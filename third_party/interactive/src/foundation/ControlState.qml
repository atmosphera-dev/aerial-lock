// SPDX-License-Identifier: LGPL-3.0-or-later
pragma Singleton
import QtQml

QtObject {
    // Pure template-state selection only. All color recipes live in the table.
    function of(enabled: bool, down: bool, hovered: bool, selected: bool,
                active: bool, insetPress: bool): int {
        if (!enabled)
            return Presentation.Disabled;
        if (down && !insetPress)
            return selected && active ? Presentation.SelectedPressed : Presentation.Pressed;
        if (selected) {
            if (!active)
                return hovered ? Presentation.InactiveHover : Presentation.Inactive;
            return hovered ? Presentation.SelectedHover : Presentation.Selected;
        }
        return hovered ? Presentation.Hover : Presentation.Rest;
    }
}
