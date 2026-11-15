// Execute ConfigStore's shipped methods with only its FileView boundary faked,
// like test-idle-config.py's extraction of the actual QML merge methods.
const test = require('node:test')
const assert = require('node:assert/strict')
const fs = require('node:fs')
const path = require('node:path')
const vm = require('node:vm')

const source = fs.readFileSync(path.join(__dirname, '../Services/ConfigStore.qml'), 'utf8')
const methods = source.match(/^    function .*?^    }/gms).join('\n')
const english = JSON.parse(fs.readFileSync(path.join(__dirname, '../Config/i18n/en.json'), 'utf8'))

function load(user, bundled = '{"prompt":"Bundled prompt","authFailed":"Bundled failure"}', lang = 'test') {
    const files = {
        '/bundle/en.json': JSON.stringify(english),
        ['/bundle/' + lang + '.json']: lang === 'en' ? JSON.stringify(english) : bundled,
        ['/user/i18n/' + lang + '.json']: user,
    }
    const warnings = []
    const context = vm.createContext({
        root: {}, i18nDir: '/bundle', configDir: '/user',
        i18nFile: { path: '', waitForJob: () => true, text() { return files[this.path] || '' } },
        Logger: { w: (tag, message) => warnings.push(tag + ': ' + message) },
    })
    vm.runInContext(methods, context)
    context.loadI18n(lang)
    return { strings: JSON.parse(JSON.stringify(context.root.i18n)), warnings }
}

test('user wins per key, then bundled language, then bundled English', () => {
    const { strings, warnings } = load(JSON.stringify({ prompt: 'My prompt' }))
    assert.equal(strings.prompt, 'My prompt')
    assert.equal(strings.authFailed, 'Bundled failure')
    assert.equal(strings.authenticating, english.authenticating)
    assert.equal(warnings.length, 0)
})

test('English itself accepts per-key user overrides', () => {
    const { strings, warnings } = load('{"prompt":"My English prompt"}', undefined, 'en')
    assert.equal(strings.prompt, 'My English prompt')
    assert.equal(strings.authFailed, english.authFailed)
    assert.equal(warnings.length, 0)
})

for (const [name, user] of Object.entries({
    missing: undefined, empty: '', malformed: '{', null: 'null', array: '[]', scalar: '42',
})) {
    test(name + ' user file warns once and keeps both bundled layers', () => {
        const { strings, warnings } = load(user)
        assert.equal(strings.prompt, 'Bundled prompt')
        assert.equal(strings.authenticating, english.authenticating)
        assert.equal(warnings.length, 1)
        assert.match(warnings[0], /\/user\/i18n\/test\.json/)
    })
}

test('invalid or blank leaves never replace usable strings; valid siblings survive', () => {
    const { strings, warnings } = load(JSON.stringify({
        prompt: '', authFailed: null, authError: 7, maxTries: [], timedOut: {},
        unlocked: '   ', dismissDebug: 'My dismiss',
    }))
    assert.equal(strings.prompt, 'Bundled prompt')
    assert.equal(strings.authFailed, 'Bundled failure')
    for (const key of ['authError', 'maxTries', 'timedOut', 'unlocked'])
        assert.equal(strings[key], english[key])
    assert.equal(strings.dismissDebug, 'My dismiss')
    assert.equal(warnings.length, 1)
})

test('missing or broken bundled language still permits a user-only language', () => {
    for (const bundled of ['', '{', 'null', '[]', '{"prompt":""}']) {
        const { strings } = load('{"prompt":"User-only language"}', bundled)
        assert.equal(strings.prompt, 'User-only language')
        assert.equal(strings.authFailed, english.authFailed)
    }
})
