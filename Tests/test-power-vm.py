#!/usr/bin/env python3
"""Power acceptance discovery is independent of unrelated permissions."""
import importlib.util
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('power_vm', Path(__file__).with_name('power-actions-vm.py'))
vm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vm)


class ActionReached(Exception):
    pass


class SupportedDiscovery(unittest.TestCase):
    def acceptance(self, snapshots):
        acceptance = vm.Acceptance.__new__(vm.Acceptance)
        acceptance.launch = lambda theme: None
        acceptance.records = []
        acceptance.record = lambda cell, **values: acceptance.records.append(dict(cell=cell, **values))
        acceptance.unlocked = False
        acceptance.unlock = lambda: setattr(acceptance, 'unlocked', True)
        acceptance.s = SimpleNamespace(env={})

        def state(predicate):
            for snapshot in snapshots:
                if predicate(snapshot):
                    acceptance.selected = snapshot
                    return snapshot
            raise AssertionError('discovery wait depends on unrelated reboot permission')
        acceptance.state = state
        return acceptance

    def snapshot(self, action, ready, allowed, reboot=False):
        state = dict(canSuspend=False, canHibernate=False, canReboot=reboot, canPowerOff=False,
                     suspendReason='Checking availability...', hibernateReason='Checking availability...',
                     rebootReason='Checking availability...', powerOffReason='Checking availability...',
                     checking='Checking availability...')
        capability = vm.ACTION_CAPABILITY[action]
        state[capability] = allowed
        reason = capability[3].lower() + capability[4:] + 'Reason'
        state[reason] = '' if allowed else 'This action is not available.' if ready else state['checking']
        return state

    def test_each_unavailable_action_does_not_require_reboot(self):
        for action in vm.ACTION_CAPABILITY:
            with self.subTest(action=action):
                pending = self.snapshot(action, False, False)
                settled = self.snapshot(action, True, False)
                acceptance = self.acceptance([pending, settled])
                with patch.object(vm, 'call', return_value='s "no"') as call:
                    self.assertEqual(acceptance.supported(action), 'UNAVAILABLE')
                self.assertIs(acceptance.selected, settled)
                self.assertTrue(acceptance.unlocked)
                self.assertEqual(acceptance.records[0]['status'], 'UNAVAILABLE')
                call.assert_called_once_with('Can' + {'suspend': 'Suspend', 'hibernate': 'Hibernate',
                                                    'reboot': 'Reboot', 'powerOff': 'PowerOff'}[action])

    def test_available_sleep_is_independent_of_reboot(self):
        for action in ('suspend', 'hibernate'):
            with self.subTest(action=action):
                acceptance = self.acceptance([self.snapshot(action, True, True)])
                with patch.object(vm, 'call', return_value='s "yes"'), \
                        patch.object(vm.driver, 'run', side_effect=ActionReached):
                    with self.assertRaises(ActionReached):
                        acceptance.supported(action)
                self.assertFalse(acceptance.records)

    def test_other_available_action_does_not_complete_target_discovery(self):
        pending = self.snapshot('suspend', False, False, reboot=True)
        settled = self.snapshot('suspend', True, False, reboot=True)
        acceptance = self.acceptance([pending, settled])
        with patch.object(vm, 'call', return_value='s "no"'):
            self.assertEqual(acceptance.supported('suspend'), 'UNAVAILABLE')
        self.assertIs(acceptance.selected, settled)


if __name__ == '__main__':
    unittest.main(verbosity=2)
