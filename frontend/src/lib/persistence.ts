export type PersistenceSchema = 'plantsim.preset' | 'plantsim.scene'

export function parsePersistenceDocument(text: string, expected: PersistenceSchema): Record<string, unknown> {
  let value: unknown
  try {
    value = JSON.parse(text)
  } catch {
    throw new Error('文件不是有效的 JSON。')
  }
  if (!value || typeof value !== 'object' || Array.isArray(value)) {
    throw new Error('持久化文件必须包含一个 JSON 对象。')
  }
  const document = value as Record<string, unknown>
  if (document.schema !== expected || document.version !== 1) {
    throw new Error(`文件 Schema 不匹配：需要 ${expected} v1。`)
  }
  return document
}

export function downloadText(fileName: string, text: string, mime = 'text/plain;charset=utf-8') {
  const url = URL.createObjectURL(new Blob([text], { type: mime }))
  const anchor = document.createElement('a')
  anchor.href = url
  anchor.download = fileName
  anchor.click()
  window.setTimeout(() => URL.revokeObjectURL(url), 0)
}

export function downloadJson(fileName: string, value: unknown) {
  downloadText(fileName, `${JSON.stringify(value, null, 2)}\n`, 'application/json;charset=utf-8')
}
