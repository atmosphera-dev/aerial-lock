// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QtTest
import InterActive
import InterActive.Components
import InterActive.Foundation as IA
import InterActive.Tests

Item {
    id: scene
    width: 640
    height: 600
    Component { id: sample; IAStatusBanner { x:24; y:24; width:540 } }
    TestCase {
        name: "StatusBanner"
        when: windowShown
        function cleanup(): void { IA.Appearance.input={}; }
        function frame(item): void { verify(TestProbe.render(item)); }
        function near(a,b,label): void { fuzzyCompare(a,b,0.5,label); }
        function test_lanes_data(): var {
            const rows=[];
            for (const scheme of ["light","dark"])
                for (const rtl of [false,true])
                    for (const touch of [false,true])
                        rows.push({tag:scheme+"-"+rtl+"-"+touch,scheme:scheme,rtl:rtl,touch:touch});
            return rows;
        }
        function test_lanes(data): void {
            failOnWarning(/.*(Binding loop|polish loop|Cannot anchor|TypeError|ReferenceError).*/);
            IA.Appearance.input={scheme:data.scheme,density:data.touch ? "touch" : "compact",fontScale:data.touch ? 1.5 : 1};
            const banner=createTemporaryObject(sample,scene,{title:"Available updates with a long heading that must elide",
                message:"Download is unavailable. Check the connection and try again without losing the current selection.",
                actionText:"Try downloading again",iconSource:IA.Appearance.icons.search});
            verify(banner !== null);
            banner.LayoutMirroring.enabled=data.rtl;
            banner.LayoutMirroring.childrenInherit=true;
            const icon=findChild(banner,"iaBannerIcon"),text=findChild(banner,"iaBannerText"),
                title=findChild(banner,"iaBannerTitle"),message=findChild(banner,"iaBannerMessage"),
                action=findChild(banner,"iaBannerAction");
            verify(icon && text && title && message && action);
            for (const width of [540,300,240,540]) {
                banner.width=width; frame(banner);
                tryCompare(icon,"available",true);
                near(icon.y+icon.height/2,text.y+text.height/2,"icon/text shared centerline");
                near(action.y+action.height/2,text.y+text.height/2,"action/text shared centerline");
                for (const item of [icon,text,action]) {
                    verify(item.x >= -0.5 && item.x+item.width <= banner.availableWidth+0.5,"all lanes remain inside banner content");
                    verify(item.width >= 0);
                }
                verify(data.rtl ? action.x+action.width <= text.x+0.5 : text.x+text.width <= action.x+0.5);
                verify(data.rtl ? text.x+text.width <= icon.x+0.5 : icon.x+icon.width <= text.x+0.5);
                compare(title.wrapMode,Text.NoWrap);
                const caption=findChild(action,"iaLabel");
                verify(caption !== null); compare(caption.wrapMode,Text.NoWrap);
                if (width === 240) {
                    verify(title.truncated,"long title elides");
                    verify(message.height > title.height,"message wraps while title stays single-line");
                }
                if (data.touch) verify(action.height >= 44,"native touch action target");
            }
            let requested=0;
            banner.actionRequested.connect(function() { ++requested; });
            banner.actionText="Retry"; frame(banner);
            compare(requested,0,"external caption synchronization is not an action");
            verify(TestProbe.activateForInput(action));
            action.forceActiveFocus(Qt.TabFocusReason);
            tryCompare(action,"activeFocus",true);
            verify(TestProbe.nativeKeyHostReady(action));
            verify(TestProbe.nativeKeyClick(action,Qt.Key_Space));
            compare(requested,1,"native keyboard action reaches the consumer");
            verify(TestProbe.click(action,action.width/2,action.height/2));
            compare(requested,2,"pointer action reaches the consumer");
            action.enabled=false;
            verify(TestProbe.click(action,action.width/2,action.height/2));
            compare(requested,2,"disabled native action does not emit");
            for (const validation of [IA.Presentation.Invalid,IA.Presentation.Warning,IA.Presentation.None]) {
                banner.validation=validation; frame(banner);
                const expected=validation === IA.Presentation.Invalid ? IA.Appearance.colors.errorSurface
                    : validation === IA.Presentation.Warning ? IA.Appearance.colors.warningSurface : IA.Appearance.colors.canvas;
                compare(banner.background.color,expected,"existing semantic banner surface");
            }
            banner.iconSource=""; banner.actionText=""; banner.title=""; frame(banner);
            compare(icon.visible,false); compare(action.visible,false); compare(title.visible,false);
            near(text.x,0,"message-only caller has no empty leading lane");
            near(text.width,banner.availableWidth,"absent action has no reserved trailing space");
            compare(message.text,banner.message);
            compare(requested,2);
        }
    }
}
