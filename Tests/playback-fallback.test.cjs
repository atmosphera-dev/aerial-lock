// Execute the production selection owner; only local file/recency I/O is faked.
const test = require('node:test')
const assert = require('node:assert/strict')
const fs = require('node:fs')
const path = require('node:path')
const vm = require('node:vm')
const source = fs.readFileSync(path.join(__dirname, '../Services/SourceCache.qml'), 'utf8')
const methods = source.match(/^    function .*?^    }/gms).join('\n')

function owner(lastPlayed, available = ['/last', '/other']) {
    const state = { playlist: [], resolved: true }
    const context = vm.createContext({
        internal: state, _preferredPaths: [], _failedPaths: [], _lastPlayedPath: lastPlayed,
        _inventory: [{ path: '/last', cached: true }, { path: '/other', cached: true }],
        managed: { available: p => available.includes(p), reportPlayback() {} },
    })
    context.root = context
    vm.runInContext(methods, context)
    return context
}
const paths = value => Array.from(value, item => item.path)

test('empty/exhausted playlist uses last played before another cached clip', () => {
    const content = owner('/last')
    content._inventory = [{ path: '/other', cached: true }, { path: '/last', cached: true }]
    assert.deepEqual(paths(content.fallbackPlaylist()), ['/last'])
})

test('gone last played uses another available cached clip', () => {
    const content = owner('/last', ['/other'])
    assert.deepEqual(paths(content.fallbackPlaylist()), ['/other'])
})

test('no available cached clip leaves the existing solid surface', () => {
    const content = owner('/last', [])
    assert.deepEqual(paths(content.fallbackPlaylist()), [])
})

test('content failure exhausts preferred paths, then last played and other cache', () => {
    const content = owner('/last', ['/preferred', '/last', '/other'])
    content._preferredPaths = ['/preferred']
    content.playbackFailed('/preferred')
    assert.deepEqual(paths(content.internal.playlist), ['/last'])
    content.playbackFailed('/last')
    assert.deepEqual(paths(content.internal.playlist), ['/other'])
    content.playbackFailed('/other')
    assert.deepEqual(paths(content.internal.playlist), [])
    content.playbackFailed('/other')
    assert.deepEqual(paths(content.internal.playlist), [], 'failed paths never retry in this episode')
})

test('last played cannot escape the frozen configured-source inventory', () => {
    const content = owner('/removed')
    assert.deepEqual(paths(content.fallbackPlaylist()), ['/last'])
    content._inventory = []
    assert.deepEqual(paths(content.fallbackPlaylist()), [])
})

test('last frame updates in-episode recency without changing the frozen pool', () => {
    const content = owner('/last')
    content.reportPlayback('/other')
    assert.deepEqual(paths(content.fallbackPlaylist()), ['/other'])
})

test('source selection excludes all disabled types before inventory and metadata indexing', () => {
    const content = owner('/disabled')
    const input = ['apple', 'community', 'folder', 'file'].flatMap(type => [
        { type, id: type + '-off', enabled: false }, { type, id: type + '-on', enabled: true }, { type, id: type + '-old' },
    ])
    const selected = content.enabledSources(input)
    assert.deepEqual(Array.from(selected, item => item.id), input.filter(item => item.enabled !== false).map(item => item.id))
    assert.equal(input.length, 12, 'selection never removes configured ownership')
    content._inventory = [] // all-off is not missing/empty configuration inheritance
    assert.deepEqual(paths(content.fallbackPlaylist()), [])
})
