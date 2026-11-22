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
    width: 560
    height: 600
    property bool captureHeaders: false
    Component { id: plain; IADialog { standardButtons: T.Dialog.Close; Rectangle { implicitWidth: 120; implicitHeight: 30; color: "#804020" } } }
    Component { id: customHeading; Item { implicitWidth: 120; implicitHeight: 17 } }
    Component { id: picker; IAColorDialog { parentWindow: scene.Window.window; colorValue: "#a649335f" } }
    Component {
        id: scrolling
        IADialog {
            width:320; height:220; title:"Download settings"; standardButtons:T.Dialog.Close
            Rectangle { implicitWidth:220; implicitHeight:600; color:"#804020" }
        }
    }
    TestCase {
        name: "DialogHeader"
        when: windowShown
        function iconIn(item): var {
            for (const child of item.children) {
                if (child instanceof IA.Icon && child.visible) return child;
                const nested = iconIn(child);
                if (nested) return nested;
            }
            return null;
        }
        function test_corner_data(): var {
            const rows = [];
            for (const kind of ["plain", "picker"])
                for (const mode of [T.Popup.Item, T.Popup.Window])
                    for (const rtl of [false, true])
                        for (const touch of [false, true])
                            for (const large of [false, true])
                                rows.push({tag: kind+"-"+mode+"-"+rtl+"-"+touch+"-"+large,
                                    kind: kind, mode: mode, rtl: rtl, touch: touch, large: large});
            return rows;
        }
        function cleanup(): void {
            scene.Window.window.contentItem.LayoutMirroring.enabled = false;
            IA.Appearance.input = {};
        }
        function test_corner(data): void {
            failOnWarning(/.*(Binding loop|polish loop|Cannot anchor|TypeError|ReferenceError).*/);
            IA.Appearance.input = {reducedMotion: true, density: data.touch ? "touch" : "compact",
                fontScale: data.large ? 1.5 : 1};
            scene.Window.window.contentItem.LayoutMirroring.enabled = data.rtl;
            scene.Window.window.contentItem.LayoutMirroring.childrenInherit = true;
            const dialog = createTemporaryObject(data.kind === "picker" ? picker : plain, scene,
                {parent: scene, popupType: data.mode, width: Math.min(532, scene.Window.window.width - 16),
                 spacing: 0, title: "Accent color with a deliberately long heading that must elide safely"});
            verify(dialog !== null);
            dialog.open();
            tryCompare(dialog, "opened", true);
            tryVerify(() => TestProbe.exposed(dialog.contentItem));
            verify(TestProbe.render(dialog.contentItem));
            const close = findChild(dialog, "iaDialogClose"), title = findChild(dialog, "iaTitle"),
                body = findChild(dialog, "iaDialogBody"), icon = iconIn(close.contentItem), band = dialog.header;
            verify(close && title && body && icon);
            const c = close.mapToItem(band, 0, 0), i = icon.mapToItem(band, 0, 0),
                t = title.mapToItem(band, 0, 0), b = body.mapToItem(band, 0, 0);
            console.info("IA_HEADER_GEOMETRY " + JSON.stringify({tag:data.tag,dpr:TestProbe.effectiveDpr(close),
                band:[band.width,band.height],close:[c.x,c.y,close.width,close.height],
                icon:[i.x,i.y,icon.width,icon.height],title:[t.x,t.y,title.width,title.height],body:[b.x,b.y,body.width,body.height]}));
            fuzzyCompare(close.width, band.height, 0.5, "Square corner slot equals the header band");
            fuzzyCompare(close.height, band.height, 0.5);
            fuzzyCompare(c.y, 0, 0.5);
            fuzzyCompare(c.x, data.rtl ? 0 : band.width-close.width, 0.5);
            fuzzyCompare(i.y, data.rtl ? i.x : band.width-i.x-icon.width, 0.5, "Equal top/trailing icon gaps");
            fuzzyCompare(i.y+icon.height/2, band.height/2, 0.5);
            fuzzyCompare(t.y+title.height/2, band.height/2, 0.5, "Title shares the icon centerline");
            fuzzyCompare(data.rtl ? t.x+title.width : t.x, data.rtl ? b.x+body.width : b.x, 0.5);
            compare(title.horizontalAlignment, data.rtl ? Text.AlignRight : Text.AlignLeft);
            verify(title.truncated);
            verify((data.rtl ? t.x-c.x-close.width : c.x-t.x-title.width) >= IA.Appearance.metrics.spacingS-0.5);
            verify(close.width >= IA.Appearance.metrics.targetSize);
            const host=TestProbe.hostWindow(close), hp=close.mapToItem(host.contentItem,0,0);
            verify(hp.x>=-0.5 && hp.y>=-0.5 && hp.x+close.width<=host.width+0.5 && hp.y+close.height<=host.height+0.5);
            const cornerX=data.rtl ? 0.5 : band.width-0.5;
            const corner=band.mapToItem(host.contentItem,cornerX,0.5);
            const before=TestProbe.renderedPixel(host.contentItem,corner.x,corner.y);
            close.forceActiveFocus(Qt.TabFocusReason);
            verify(TestProbe.render(close));
            verify(close.visualFocus);
            compare((close.background as IA.ControlBackground).focused, true);
            fuzzyCompare(TestProbe.renderedPixel(host.contentItem,corner.x,corner.y),before,2,
                "Focus remains inside the popup's rounded corner");
            verify(TestProbe.move(close,close.width/2,close.height/2));
            tryCompare(close,"hovered",true);
            verify(TestProbe.render(close));
            fuzzyCompare(TestProbe.renderedPixel(host.contentItem,corner.x,corner.y),before,2,
                "Hover does not paint outside the popup's rounded corner");
            if (scene.captureHeaders && data.mode===T.Popup.Window
                    && ((!data.rtl && !data.large) || (data.rtl && data.large && !data.touch)))
                verify(TestProbe.captureWindow(close,"header-"+data.tag+".png"));
            verify(TestProbe.click(close,close.width/2,close.height/2));
            tryCompare(dialog,"visible",false);
        }
        function test_permanentSeparator_data(): var {
            const rows=[];
            for (const mode of [T.Popup.Item,T.Popup.Window])
                for (const scheme of ["light","dark"])
                    for (const contrast of ["normal","high"])
                        for (const rtl of [false,true])
                            rows.push({tag:mode+"-"+scheme+"-"+contrast+"-"+rtl,
                                mode:mode,scheme:scheme,contrast:contrast,rtl:rtl});
            return rows;
        }
        function assertDividerOutline(dialog,line): void {
            if (!line.visible) return;
            const background=dialog.background as Rectangle;
            verify(background !== null);
            const host=TestProbe.hostWindow(line).contentItem;
            for (const x of [background.border.width/2,background.width-background.border.width/2]) {
                const p=background.mapToItem(host,x,line.mapToItem(background,0,line.height/2).y);
                fuzzyCompare(TestProbe.renderedPixel(host,p.x,p.y),background.border.color,2,
                    "divider does not overwrite the native side outline");
            }
        }
        function test_permanentSeparator(data): void {
            IA.Appearance.input={scheme:data.scheme,contrast:data.contrast};
            scene.Window.window.contentItem.LayoutMirroring.enabled=data.rtl;
            scene.Window.window.contentItem.LayoutMirroring.childrenInherit=true;
            const dialog=createTemporaryObject(scrolling,scene,{parent:scene,popupType:data.mode});
            verify(dialog !== null); dialog.open(); tryCompare(dialog,"opened",true);
            const separator=findChild(dialog,"iaDialogTitleSeparator"),top=findChild(dialog,"iaDialogTopEdge"),
                fade=findChild(dialog,"iaDialogTopFade"),view=findChild(dialog,"iaDialogViewport");
            verify(separator && top && fade && view);
            const background=dialog.background as Rectangle;
            verify(background !== null);
            try {
                tryVerify(() => TestProbe.exposed(view));
                for (const fraction of [0,0.5,1]) {
                    view.contentY=view.originY+fraction*(view.contentHeight-view.height);
                    verify(TestProbe.render(view));
                    compare(separator.visible,true,"owned title boundary is permanent");
                    compare(top.visible,false,"no second top divider while scrolling");
                    compare(fade.visible,fraction > 0,"scroll fade retains independent hidden-content meaning");
                    fuzzyCompare(separator.y+separator.height,dialog.header.height,0.5,"separator belongs to heading boundary");
                    fuzzyCompare(separator.width+2*background.border.width,dialog.header.width,0.5);
                    const host=TestProbe.hostWindow(view).contentItem;
                    const point=separator.mapToItem(host,separator.width/2,separator.height/2);
                    fuzzyCompare(TestProbe.renderedPixel(host,point.x,point.y),IA.Appearance.colors.separator,2,
                        "actual permanent separator-role pixel");
                    for (const line of [separator,top,findChild(dialog,"iaDialogBottomEdge")]) {
                        assertDividerOutline(dialog,line);
                    }
                }
                const custom=createTemporaryObject(customHeading,scene);
                verify(custom !== null); dialog.header=custom;
                verify(TestProbe.render(view));
                compare(top.visible,true,"custom heading keeps hidden-edge line");
                assertDividerOutline(dialog,top);
                dialog.header=null;
                verify(TestProbe.render(view));
                compare(top.visible,true,"absent heading keeps hidden-edge line");
                assertDividerOutline(dialog,top);
                view.contentY=view.originY;
                verify(TestProbe.render(view));
                compare(top.visible,false); compare(fade.visible,false);
            } finally { dialog.close(); }
        }
        function test_optionalHeader(): void {
            const dialog=createTemporaryObject(plain,scene,{parent:scene,width:300,title:"",standardButtons:T.Dialog.NoButton});
            verify(dialog!==null);
            dialog.open();tryCompare(dialog,"opened",true);
            verify(TestProbe.render(dialog.contentItem));
            compare(dialog.header.visible,false);
            compare(dialog.implicitHeaderHeight,0);
            dialog.title="Title without a reject action";
            verify(TestProbe.render(dialog.contentItem));
            const title=findChild(dialog,"iaTitle"),close=findChild(dialog,"iaDialogClose");
            compare(close.visible,false);
            fuzzyCompare(title.width,dialog.header.width-dialog.leftPadding-dialog.rightPadding,0.5);
            dialog.title="";dialog.standardButtons=T.Dialog.Close;
            verify(TestProbe.render(dialog.contentItem));
            compare(close.visible,true);
            fuzzyCompare(close.height,dialog.header.height,0.5);
            const custom=createTemporaryObject(customHeading,scene);
            verify(custom!==null);
            dialog.header=custom;
            verify(TestProbe.render(dialog.contentItem));
            compare(dialog.header,custom);
            fuzzyCompare(dialog.implicitHeaderHeight,17,0.5);
            dialog.close();
        }
    }
}
