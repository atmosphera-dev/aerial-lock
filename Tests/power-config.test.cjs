// Execute ConfigStore's shipped merge/schema methods without its startup I/O.
const test = require('node:test')
const assert = require('node:assert/strict')
const fs = require('node:fs')
const path = require('node:path')
const vm = require('node:vm')

const source = fs.readFileSync(path.join(__dirname, '../Services/ConfigStore.qml'), 'utf8')
const methods = source.match(/^    function .*?^    }/gms).join('\n')
const schema = source.split('    readonly property var schema: (')[1].split('\n    })')[0] + '\n    }'
const defaults = JSON.parse(fs.readFileSync(path.join(__dirname, '../Config/defaults.json'), 'utf8'))

for (const [name, user, enabled, warningCount] of [
    ['missing', {}, true, 0], ['partial', { powerActions: {} }, true, 0],
    ['false', { powerActions: { enabled: false } }, false, 0],
    ['true', { powerActions: { enabled: true } }, true, 0],
    ['null object', { powerActions: null }, true, 1],
    ['array object', { powerActions: [] }, true, 1],
    ['scalar object', { powerActions: false }, true, 1],
    ['string leaf', { powerActions: { enabled: 'false' } }, true, 1],
    ['null leaf', { powerActions: { enabled: null } }, true, 1],
    ['number leaf', { powerActions: { enabled: 0 } }, true, 1],
    ['siblings', { powerActions: { enabled: false, extra: 9 }, idle: { timeoutSeconds: 42 } }, false, 0],
]) {
    test('powerActions ConfigStore: ' + name, () => {
        const warnings = []
        const context = vm.createContext({ Logger: { w: (tag, message) => warnings.push(message) } })
        vm.runInContext(methods, context)
        const before = JSON.stringify(user)
        context.defaults = structuredClone(defaults)
        context.user = user
        assert.equal(context.migrateUserConfig(user), false, 'powerActions never requests a migration rewrite')
        vm.runInContext(`
            var schema = (${schema});
            if (!checkBundle(defaults, schema, '')) throw Error('invalid bundle');
            var merged = deepMerge(defaults, user);
            sanitise(merged, defaults, schema, '');
        `, context)
        assert.equal(context.merged.powerActions.enabled, enabled)
        assert.equal(JSON.stringify(user), before, 'merge never rewrites the user config')
        assert.equal(warnings.length, warningCount)
        assert.equal(context.merged.idle.timeoutSeconds, user.idle?.timeoutSeconds ?? defaults.idle.timeoutSeconds)
        assert.equal(JSON.stringify(context.merged.panel), JSON.stringify(defaults.panel))
        if (name === 'siblings') assert.equal(context.merged.powerActions.extra, 9)
    })
}
