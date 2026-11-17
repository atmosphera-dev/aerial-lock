// Runtime settings cases moved intact to companion-config-probe.cpp, where
// they exercise the sole production reader instead of extracted JS copies.
const test = require('node:test')
const assert = require('node:assert/strict')
const fs = require('node:fs')
const path = require('node:path')

test('ConfigStore delegates settings to the read-only facade, not a second writer/schema', () => {
    const source = fs.readFileSync(path.join(__dirname, '../Services/ConfigStore.qml'), 'utf8')
    assert.match(source, /LocalConfig\s*\{/)
    assert.match(source, /local\.load\(Quickshell\.shellDir, configPath\)/)
    assert.match(source, /root\.data = local\.data/)
    for (const obsolete of ['setText(', 'migrateUserConfig(', 'maybeWritePreferred(', 'sanitise(', 'property var schema'])
        assert.equal(source.includes(obsolete), false, obsolete + ' must have only one owner')
})
