<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import PlantViewport from './components/PlantViewport.vue'
import GrowthChart from './components/GrowthChart.vue'
import GrowthTimeline from './components/GrowthTimeline.vue'
import MetricsPanel from './components/MetricsPanel.vue'
import ReplayEventLog from './components/ReplayEventLog.vue'
import PlantEditPanel from './components/PlantEditPanel.vue'
import PersistencePanel from './components/PersistencePanel.vue'
import {
  appendFrame,
  asNumber,
  findNearest,
  normalizeFrames,
  offlineFrames,
  parseStages,
  stageListFromFrames,
  stageTitle,
  toPoint,
  type Metric,
  type Point,
  type Stage,
} from './lib/growthData'
import { downloadGrowthExport, makeGrowthExport } from './lib/growthExport'
import { downloadJson, downloadText } from './lib/persistence'
import { useEngineSocket } from './composables/useEngineSocket'

type Tool = 'select' | 'orbit' | 'wind'
type State = Point & { speed: number; mode: number; nodeCount: number; recordedFrameCount: number; recordedEndAge: number }
type Log = { time: string; text: string; tone: 'ok' | 'warn' | 'muted' }

const light = ref(.8), wind = ref(.28), tool = ref<Tool>('orbit'), resetToken = ref(0), playing = ref(false), speed = ref(1), age = ref(0), metric = ref<Metric>('height')
const logs = ref<Log[]>([])
const editCanUndo = ref(false)
let editRequestId = 0
let persistenceRequestId = 0
const pendingAction = ref<string | null>(null)
const history = ref<Point[]>(offlineFrames())
const state = ref<State>({ ...history.value[0], speed: 1, mode: 0, nodeCount: 1, recordedFrameCount: history.value.length, recordedEndAge: 30 })
const names: Record<Metric, string> = { height: '植物高度', totalBranchLength: '枝干总长度', branchCount: '分枝数量', leafCount: '叶片数量', canopyWidth: '冠幅' }
const stages = ref<Stage[]>(stageListFromFrames(history.value))
const label = computed(() => ({
  connecting: '正在连接引擎',
  connected: '引擎已连接',
  reconnecting: `正在重连（${reconnectDelay.value}s）`,
  offline: '离线回放预览',
}[connection.value]))
const maxAge = computed(() => Math.max(30, state.value.recordedEndAge, history.value.at(-1)?.age ?? 0))
const atLatest = computed(() => Math.abs(age.value - (history.value.at(-1)?.age ?? maxAge.value)) < 0.0001)
const connectionNote = computed(() => {
  if (pendingAction.value) return `正在等待引擎确认：${pendingAction.value}`
  if (connection.value === 'connecting') return '正在建立与引擎的连接。'
  if (connection.value === 'offline') return '当前为离线回放，新操作不会同步到引擎。'
  if (connection.value === 'reconnecting') return '正在恢复与引擎的连接。'
  return '记录和回放状态已与引擎同步。'
})
const progress = computed(() => Math.min(100, age.value / maxAge.value * 100))
const current = computed(() => ({ height: state.value.height, totalBranchLength: state.value.totalBranchLength, branchCount: state.value.branchCount, leafCount: state.value.leafCount, canopyWidth: state.value.canopyWidth }))
const viewportGrowthProgress = computed(() => Math.max(0, Math.min(1, age.value / Math.max(0.001, maxAge.value))))
let offlinePlaybackFrame = 0
let offlinePlaybackStartedAt = 0
let confirmationTimer = 0

function log(text: string, tone: Log['tone'] = 'muted') { logs.value.unshift({ time: new Intl.DateTimeFormat('zh-CN', { hour: '2-digit', minute: '2-digit', second: '2-digit' }).format(new Date()), text, tone }); logs.value = logs.value.slice(0, 6) }
const { connection, reconnectDelay, isConnected: isEngineConnected, send, connect, dispose } = useEngineSocket({
  url: import.meta.env.VITE_ENGINE_WS_URL || 'ws://127.0.0.1:4317',
  onMessage: receive,
  onConnected: () => { send({ type: 'request_growth_data' }); log('已连接 C++ 生长引擎。', 'ok') },
  onDisconnected: () => { clearConfirmation(); playing.value = false },
  onLog: log,
})
function clearConfirmation() { if (confirmationTimer) window.clearTimeout(confirmationTimer); confirmationTimer = 0; pendingAction.value = null }
function awaitConfirmation(action: string) {
  if (confirmationTimer) window.clearTimeout(confirmationTimer)
  pendingAction.value = action
  confirmationTimer = window.setTimeout(() => {
    if (!pendingAction.value) return
    log(`引擎尚未确认“${pendingAction.value}”操作，界面将继续以最后接收的状态为准。`, 'warn')
    pendingAction.value = null
    confirmationTimer = 0
  }, 2500)
}
function stopOfflinePlayback() { if (offlinePlaybackFrame) cancelAnimationFrame(offlinePlaybackFrame); offlinePlaybackFrame = 0; offlinePlaybackStartedAt = 0 }
function runOfflinePlayback(timestamp: number) {
  if (isEngineConnected.value || !playing.value) { stopOfflinePlayback(); return }
  if (!offlinePlaybackStartedAt) offlinePlaybackStartedAt = timestamp
  const deltaSeconds = Math.max(0, (timestamp - offlinePlaybackStartedAt) / 1000)
  offlinePlaybackStartedAt = timestamp
  const nextAge = Math.min(maxAge.value, age.value + deltaSeconds * 0.75 * speed.value)
  localSeek(nextAge)
  if (nextAge >= maxAge.value - 0.0001) { playing.value = false; stopOfflinePlayback(); log('离线回放已到达最后一个记录帧。', 'ok'); return }
  offlinePlaybackFrame = requestAnimationFrame(runOfflinePlayback)
}
function startOfflinePlayback() { stopOfflinePlayback(); offlinePlaybackFrame = requestAnimationFrame(runOfflinePlayback) }
function nearest(target: number) { return findNearest(history.value, target) }
function localSeek(target: number) { const x = nearest(target); if (!x) return; age.value = x.age; const point = { ...x, plantState: x.plantState ?? state.value.plantState }; state.value = { ...state.value, ...point, recordedFrameCount: history.value.length, recordedEndAge: history.value.at(-1)?.age ?? point.age } }
function append(x: Point) { history.value = appendFrame(history.value, x) }

let pendingGrowthState: Record<string, unknown> | null = null
let growthStateFrame = 0
function applyGrowthState(payload: Record<string, unknown>) {
  const x = toPoint(payload)
  if (!x) return
  const point = { ...x, plantState: x.plantState ?? state.value.plantState }
  state.value = { ...point, speed: asNumber(payload.speed, 1), mode: asNumber(payload.mode), nodeCount: asNumber(payload.nodeCount), recordedFrameCount: asNumber(payload.recordedFrameCount, history.value.length), recordedEndAge: asNumber(payload.recordedEndAge, point.age) }
  age.value = point.age
  speed.value = state.value.speed
  playing.value = state.value.mode === 1
  append(point)
  clearConfirmation()
}
function queueGrowthState(payload: Record<string, unknown>) {
  pendingGrowthState = payload
  if (growthStateFrame) return
  growthStateFrame = requestAnimationFrame(() => {
    growthStateFrame = 0
    const latest = pendingGrowthState
    pendingGrowthState = null
    if (latest) applyGrowthState(latest)
  })
}
function applyImmediateGrowthState(payload: Record<string, unknown>) {
  if (growthStateFrame) cancelAnimationFrame(growthStateFrame)
  growthStateFrame = 0
  pendingGrowthState = null
  applyGrowthState(payload)
}
function receive(payload: Record<string, unknown>) {
  if (payload.type === 'error') {
    const code = payload.code ? `[${String(payload.code)}] ` : ''
    log(`引擎错误：${code}${String(payload.message ?? '未知错误')}`, 'warn')
    clearConfirmation()
    return
  }
  if (payload.type === 'environment_updated') {
    light.value = asNumber(payload.lightIntensity, light.value)
    return
  }
  if (payload.type === 'growth_data' && Array.isArray(payload.frames)) {
    const frames = normalizeFrames(payload.frames.map((x, index) => toPoint(x, index)).filter((x): x is Point => !!x))
    if (frames.length) {
      history.value = frames
      stages.value = parseStages(payload.keyStages, frames)
      localSeek(age.value)
      log(`已载入 ${frames.length} 帧生长记录。`, 'ok')
    }
    return
  }
  if (payload.type === 'plant.edit.updated') {
    editCanUndo.value = payload.canUndo === true
    clearConfirmation()
    log(`Edit synchronized: revision ${asNumber(payload.revision)}, mesh ${asNumber(payload.meshVersion)}`, 'ok')
    return
  }
  if (payload.type === 'persistence.preset' && payload.preset && typeof payload.preset === 'object') {
    downloadJson('plantsim-preset.json', payload.preset)
    clearConfirmation()
    log('植物与环境预设已保存。', 'ok')
    return
  }
  if (payload.type === 'persistence.scene' && payload.scene && typeof payload.scene === 'object') {
    downloadJson('plantsim-scene.json', payload.scene)
    clearConfirmation()
    log('完整场景归档已保存。', 'ok')
    return
  }
  if (payload.type === 'persistence.obj') {
    const obj = typeof payload.obj === 'string' ? payload.obj : ''
    const mtl = typeof payload.mtl === 'string' ? payload.mtl : ''
    if (obj && mtl) {
      downloadText(String(payload.mtlFileName ?? 'plantsim_scene.mtl'), mtl)
      downloadText(String(payload.objFileName ?? 'plantsim_scene.obj'), obj)
      clearConfirmation()
      log(`已导出完整 OBJ/MTL 场景（枝干 ${asNumber(payload.branchTriangles)}、叶片 ${asNumber(payload.leafTriangles)} 个三角形）。`, 'ok')
    }
    return
  }
  if (payload.type === 'persistence.updated') {
    clearConfirmation()
    send({ type: 'request_growth_data' })
    const action = String(payload.action ?? 'scene_updated')
    log(`场景持久化操作完成：${action}。`, 'ok')
    return
  }
  if (payload.type === 'growth_state') {
    // Seek/stage responses carry a complete plant state and must be applied at
    // once; ordinary high-frequency metric broadcasts are merged per paint.
    if (payload.plantState) applyImmediateGrowthState(payload)
    else queueGrowthState(payload)
  }
}

function nextEditRequestId() { editRequestId += 1; return `edit-${editRequestId}` }
function nextPersistenceRequestId() { persistenceRequestId += 1; return `persistence-${persistenceRequestId}` }
function sendEditBegin(payload: { nodeId: number; tool: string }) {
  if (!send({ type: 'edit.begin', requestId: nextEditRequestId(), plantId: 1, nodeId: payload.nodeId, mode: 'node', tool: payload.tool })) log('Edit command was not sent because the engine is offline.', 'warn')
}
function sendEditUpdate(payload: { nodeId: number; tool: string; preview?: boolean; params?: Record<string, unknown> }) {
  if (!send({ type: 'edit.update', requestId: nextEditRequestId(), plantId: 1, nodeId: payload.nodeId, mode: 'node', tool: payload.tool, preview: payload.preview === true, params: payload.params ?? {} })) log('Edit update was not sent because the engine is offline.', 'warn')
}
function sendEditCommit(payload: { nodeId: number }) {
  if (send({ type: 'edit.commit', requestId: nextEditRequestId(), plantId: 1, nodeId: payload.nodeId, mode: 'node' })) awaitConfirmation('Commit edit')
}
function undoEdit() { if (send({ type: 'edit.undo', requestId: nextEditRequestId(), plantId: 1 })) awaitConfirmation('Undo edit') }
function resetPlantEdit() { if (send({ type: 'edit.reset', requestId: nextEditRequestId(), plantId: 1 })) awaitConfirmation('Reset plant') }
function previewSeek(target: number) { age.value = target; localSeek(target) }
function commitSeek(target: number) { if (send({ type: 'growth_seek', age: target })) { awaitConfirmation('\u5b9a\u4f4d\u56de\u653e'); log(`\u5df2\u8bf7\u6c42\u5b9a\u4f4d\u5230 ${target.toFixed(2)} \u5e74\uff0c\u7b49\u5f85\u5f15\u64ce\u786e\u8ba4\u3002`) } else log(`\u79bb\u7ebf\u5b9a\u4f4d\u5230 ${target.toFixed(2)} \u5e74\u3002`, 'ok') }
function stage(x: Stage) { age.value = x.age; localSeek(x.age); if (send({ type: 'growth_seek', age: x.age })) { awaitConfirmation(`跳转${stageTitle(x)}`); log(`已请求跳转到关键阶段：${stageTitle(x)}。`) } else log(`离线跳转到关键阶段：${stageTitle(x)}。`, 'ok') }
function toggle() {
  if (isEngineConnected.value) {
    const command = playing.value ? 'growth_pause' : 'growth_resume'
    if (send({ type: command })) { awaitConfirmation(playing.value ? '暂停回放' : '开始回放'); log(`已请求${playing.value ? '暂停' : '开始'}生长过程回放。`) }
    return
  }
  if (playing.value) { playing.value = false; stopOfflinePlayback(); log('已暂停离线回放。') }
  else { if (age.value >= maxAge.value - 0.0001) localSeek(0); playing.value = true; startOfflinePlayback(); log('开始离线生长过程回放。', 'ok') }
}
function reset() { age.value = 0; localSeek(0); if (send({ type: 'growth_reset' })) { awaitConfirmation('重置回放'); log('已请求重置生长记录。') } else { playing.value = false; stopOfflinePlayback(); log('已重置离线回放。', 'ok') } }
function jumpToLatest() {
  const target = history.value.at(-1)?.age ?? maxAge.value
  age.value = target
  localSeek(target)
  if (send({ type: 'growth_seek', age: target })) { awaitConfirmation('\u56de\u5230\u6700\u65b0\u5e27'); log(`\u5df2\u8bf7\u6c42\u8fd4\u56de\u6700\u65b0\u8bb0\u5f55\u5e27\uff1a${target.toFixed(2)} \u5e74\u3002`) }
  else log(`\u5df2\u8fd4\u56de\u6700\u65b0\u8bb0\u5f55\u5e27\uff1a${target.toFixed(2)} \u5e74\u3002`, 'ok')
}
function setSpeed(requested: number) { speed.value = requested; if (send({ type: 'growth_speed', speed: requested })) awaitConfirmation('\u8bbe\u7f6e\u56de\u653e\u901f\u5ea6') }
function setLight(e: Event) { light.value = +(e.target as HTMLInputElement).value; send({ type: 'adjust_light', value: light.value }) }
function commitChartSeek(target: number) {
  age.value = target
  localSeek(target)
  if (send({ type: 'growth_seek', age: target })) { awaitConfirmation('\u56fe\u8868\u5b9a\u4f4d'); log(`\u5df2\u8bf7\u6c42\u4ece\u56fe\u8868\u5b9a\u4f4d\u5230 ${target.toFixed(2)} \u5e74\u3002`) }
  else log(`\u5df2\u4ece\u56fe\u8868\u5b9a\u4f4d\u5230 ${target.toFixed(2)} \u5e74\u3002`, 'ok')
}
function exportData(kind: 'json' | 'csv') { downloadGrowthExport(makeGrowthExport(kind, history.value)); log(`\u5df2\u5bfc\u51fa\u751f\u957f\u6307\u6807 ${kind.toUpperCase()}\u3002`, 'ok') }
function requestPresetSave() {
  if (send({ type: 'persistence.preset.request', requestId: nextPersistenceRequestId() })) awaitConfirmation('保存预设')
}
function importPreset(preset: Record<string, unknown>) {
  if (send({ type: 'persistence.preset.import', requestId: nextPersistenceRequestId(), preset })) awaitConfirmation('载入预设')
}
function requestSceneSave() {
  if (send({ type: 'persistence.scene.request', requestId: nextPersistenceRequestId() })) awaitConfirmation('保存场景')
}
function importScene(scene: Record<string, unknown>) {
  if (send({ type: 'persistence.scene.import', requestId: nextPersistenceRequestId(), scene })) awaitConfirmation('载入场景')
}
function restoreSceneAtCurrentAge() {
  if (send({ type: 'persistence.scene.restore', requestId: nextPersistenceRequestId(), age: age.value })) awaitConfirmation('恢复时间点')
}
function requestObjExport() {
  if (send({ type: 'persistence.obj.request', requestId: nextPersistenceRequestId() })) awaitConfirmation('导出 OBJ/MTL')
}
function persistenceError(message: string) { log(`文件载入失败：${message}`, 'warn') }
onMounted(() => { localSeek(0); connect(true) }); onBeforeUnmount(() => { if (growthStateFrame) cancelAnimationFrame(growthStateFrame); stopOfflinePlayback(); clearConfirmation(); dispose() })
</script>

<template>
  <div class="week12-shell">
    <header class="week12-header"><div><p class="eyebrow">WEEK 12 · GROWTH DATA LAB</p><h1>生长数据记录与回放</h1><p>逐时间步保存植物状态，回放生长过程并追踪结构指标变化。</p><p class="connection-note" aria-live="polite">{{ connectionNote }}</p></div><div class="header-actions"><span class="chip" :class="connection" role="status"><i></i>{{ label }}</span><button type="button" class="ghost" @click="connect(true)">重新连接</button></div></header>
    <section class="metrics"><article class="stat accent"><span>生长年龄</span><strong>{{ state.age.toFixed(2) }}<small> 年</small></strong><em>{{ state.lifeStage }}</em></article><article class="stat"><span>植物高度</span><strong>{{ current.height.toFixed(2) }}<small> m</small></strong><em>HEIGHT</em></article><article class="stat"><span>枝干总长度</span><strong>{{ current.totalBranchLength.toFixed(1) }}<small> m</small></strong><em>BRANCH LENGTH</em></article><article class="stat"><span>分枝 / 叶片</span><strong>{{ current.branchCount }}<small> / {{ current.leafCount }}</small></strong><em>STRUCTURE</em></article><article class="stat"><span>冠幅</span><strong>{{ current.canopyWidth.toFixed(2) }}<small> m</small></strong><em>CANOPY WIDTH</em></article></section>
    <main class="main-grid"><section class="card viewport-card"><header><span>01</span><h2>植物生长预览</h2><b>{{ playing ? 'REPLAYING' : 'PAUSED' }}</b></header><PlantViewport :light-intensity="light" :wind-intensity="wind" :playing="playing" plant-type="cherry" :interaction-mode="tool" :reset-token="resetToken" :snapshot="state.plantState" :growth-progress="viewportGrowthProgress"/><footer><div class="tools"><button type="button" :class="{ active: tool === 'select' }" :aria-pressed="tool === 'select'" @click="tool = 'select'">选择</button><button type="button" :class="{ active: tool === 'orbit' }" :aria-pressed="tool === 'orbit'" @click="tool = 'orbit'">旋转</button><button type="button" :class="{ active: tool === 'wind' }" :aria-pressed="tool === 'wind'" @click="tool = 'wind'">风场</button></div><label>光照 <input type="range" min="0" max="1" step=".01" :value="light" @input="setLight"/></label><button type="button" class="ghost" @click="resetToken += 1">复位视角</button></footer></section><MetricsPanel :recorded-frame-count="state.recordedFrameCount" :recorded-end-age="state.recordedEndAge" :speed="speed" :node-count="state.nodeCount" @speed-change="setSpeed" @export="exportData"/><PlantEditPanel :snapshot="state.plantState" :connected="isEngineConnected" :can-undo="editCanUndo" @begin="sendEditBegin" @update="sendEditUpdate" @commit="sendEditCommit" @undo="undoEdit" @reset="resetPlantEdit"/><PersistencePanel :connected="isEngineConnected" :busy="!!pendingAction" :age="age" @save-preset="requestPresetSave" @import-preset="importPreset" @save-scene="requestSceneSave" @import-scene="importScene" @restore-scene="restoreSceneAtCurrentAge" @export-obj="requestObjExport" @error="persistenceError"/></main>
    <GrowthTimeline :age="age" :max-age="maxAge" :progress="progress" :playing="playing" :pending="!!pendingAction" :at-latest="atLatest" :stages="stages" @toggle="toggle" @preview="previewSeek" @seek="commitSeek" @reset="reset" @latest="jumpToLatest" @stage="stage"/>
    <GrowthChart :history="history" :metric="metric" :metric-names="names" :current-value="current[metric]" :age="age" :max-age="maxAge" :stages="stages" @update:metric="metric = $event" @seek="commitChartSeek"/>
    <ReplayEventLog :logs="logs"/>
  </div>
</template>
