import { describe, expect, it } from 'vitest'
import { parsePersistenceDocument } from './persistence'

describe('persistence document validation', () => {
  it('accepts the expected schema and version', () => {
    const value = parsePersistenceDocument('{"schema":"plantsim.preset","version":1}', 'plantsim.preset')
    expect(value.schema).toBe('plantsim.preset')
  })

  it('rejects malformed JSON and mismatched scene files', () => {
    expect(() => parsePersistenceDocument('{', 'plantsim.scene')).toThrow('JSON')
    expect(() => parsePersistenceDocument('{"schema":"plantsim.preset","version":1}', 'plantsim.scene'))
      .toThrow('Schema')
  })
})
