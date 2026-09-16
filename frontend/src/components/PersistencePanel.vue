<script setup lang="ts">
import { ref } from 'vue'
import { parsePersistenceDocument, type PersistenceSchema } from '../lib/persistence'

const props = defineProps<{ connected: boolean; busy: boolean; age: number }>()
const emit = defineEmits<{
  savePreset: []
  importPreset: [value: Record<string, unknown>]
  saveScene: []
  importScene: [value: Record<string, unknown>]
  exportObj: []
  restoreScene: []
  error: [message: string]
}>()

const presetInput = ref<HTMLInputElement | null>(null)
const sceneInput = ref<HTMLInputElement | null>(null)

async function importFile(event: Event, schema: PersistenceSchema, kind: 'preset' | 'scene') {
  const input = event.target as HTMLInputElement
  const file = input.files?.[0]
  input.value = ''
  if (!file) return
  try {
    const value = parsePersistenceDocument(await file.text(), schema)
    if (kind === 'preset') emit('importPreset', value)
    else emit('importScene', value)
  } catch (error) {
    emit('error', error instanceof Error ? error.message : '无法读取持久化文件。')
  }
}
</script>

<template>
  <section class="card persistence-card">
    <header><span>04</span><h2>场景与预设</h2><b>WEEK 16</b></header>
    <div class="persistence-body">
      <div class="persistence-group">
        <div><strong>植物预设</strong><small>植物参数 + 环境参数</small></div>
        <button type="button" class="ghost" :disabled="!connected || busy" @click="emit('savePreset')">保存 JSON</button>
        <button type="button" class="ghost" :disabled="!connected || busy" @click="presetInput?.click()">载入</button>
      </div>
      <div class="persistence-group">
        <div><strong>完整场景</strong><small>结构、环境、回放记录与状态</small></div>
        <button type="button" class="ghost" :disabled="!connected || busy" @click="emit('saveScene')">保存归档</button>
        <button type="button" class="ghost" :disabled="!connected || busy" @click="sceneInput?.click()">载入</button>
      </div>
      <div class="persistence-group">
        <div><strong>时间点 / 模型</strong><small>当前 {{ props.age.toFixed(2) }} 年</small></div>
        <button type="button" class="ghost" :disabled="!connected || busy" @click="emit('restoreScene')">恢复此帧</button>
        <button type="button" class="primary" :disabled="!connected || busy" @click="emit('exportObj')">导出 OBJ</button>
      </div>
    </div>
    <input ref="presetInput" class="sr-only" type="file" accept="application/json,.json" @change="importFile($event, 'plantsim.preset', 'preset')" />
    <input ref="sceneInput" class="sr-only" type="file" accept="application/json,.json" @change="importFile($event, 'plantsim.scene', 'scene')" />
  </section>
</template>

<style scoped>
.persistence-card { grid-column:2; }
.persistence-body { display:grid; gap:10px; padding:14px 18px 18px; }
.persistence-group { display:grid; grid-template-columns:minmax(0,1fr) auto auto; gap:8px; align-items:center; }
.persistence-group div { display:grid; gap:3px; }
.persistence-group strong { color:#d7e9dc; font-size:12px; }
.persistence-group small { color:#789082; font-size:10px; }
.persistence-group button { padding:7px 9px; white-space:nowrap; }
@media(max-width:1040px) { .persistence-card { grid-column:1; } }
@media(max-width:480px) { .persistence-group { grid-template-columns:1fr 1fr; } .persistence-group div { grid-column:span 2; } }
</style>
