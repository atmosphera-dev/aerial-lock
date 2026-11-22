// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QtTest
import InterActive
import InterActive.Foundation as IA
import InterActive.Tests

Item {
    id: scene
    width: 1000
    height: 800
    Component { id: boxComponent; IADialogButtonBox { width:340; height:72 } }
    Component { id: customComponent; IAButton { text:"Custom"; T.DialogButtonBox.buttonRole:T.DialogButtonBox.AcceptRole } }
    Component {
        id: dialogComponent
        IADialog {
            width:340
            title:"Shared footer"
            Column { width:parent.width; IATextField { width:parent.width; text:"Retained draft" } }
        }
    }
    // Native template baseline for vertical invalidation only, not a second IA
    // implementation: no appearance, semantic-query or principal-action policy.
    Component {
        id: nativeComponent
        T.DialogButtonBox {
            id: nativeBox
            x:500; width:340; height:72
            leftPadding:12; rightPadding:12; topPadding:6; bottomPadding:6; spacing:8
            alignment:Qt.AlignRight
            implicitHeight:contentHeight + topPadding + bottomPadding
            contentWidth:nativeRow.contentWidth
            contentItem: ListView {
                id:nativeRow
                implicitWidth:contentWidth
                model:nativeBox.contentModel
                spacing:nativeBox.spacing
                orientation:ListView.Horizontal
                interactive:false
            }
        }
    }
    TestCase {
        name:"DialogButtonBoxLayout"
        when:windowShown
        function init(): void {
            IA.Appearance.input={scheme:"light",contrast:"normal",reducedMotion:true};
            scene.LayoutMirroring.enabled=false;
            scene.LayoutMirroring.childrenInherit=true;
            failOnWarning(/.*([Aa]nchor|[Bb]inding loop|[Pp]olish.*loop).*/);
        }
        function frame(box): void {
            tryVerify(function() { return TestProbe.exposed(box); });
            verify(TestProbe.render(box));
        }
        function near(a,b,label): void { fuzzyCompare(a,b,0.5,label); }
        function extent(box): real {
            let width=Math.max(0,box.count-1)*box.spacing;
            for (let i=0;i<box.count;++i) width+=box.itemAt(i).width;
            return width;
        }
        function mirrors(box,root,row): void {
            box.LayoutMirroring.enabled=root;
            box.LayoutMirroring.childrenInherit=true;
            box.contentItem.LayoutMirroring.enabled=row;
        }
        function assertGeometry(box): void {
            frame(box);
            const row=box.contentItem, actual=extent(box);
            let flags=box.alignment;
            if (!(flags & Qt.AlignHorizontal_Mask)) flags|=Qt.AlignLeft;
            if (!(flags & Qt.AlignAbsolute) && (flags & (Qt.AlignLeft|Qt.AlignRight)) && box.mirrored)
                flags^=Qt.AlignLeft|Qt.AlignRight;
            let left=box.leftPadding;
            if (box.alignment !== 0) {
                if (flags & Qt.AlignRight) left+=Math.max(0,box.availableWidth-actual);
                else if (flags & Qt.AlignHCenter) left+=Math.max(0,(box.availableWidth-actual)/2);
            } else if (row.effectiveLayoutDirection === Qt.RightToLeft) {
                left+=box.availableWidth-actual;
            }
            near(row.x,box.leftPadding,"native viewport starts at physical padding");
            near(row.width,box.alignment ? Math.max(actual,box.availableWidth) : box.availableWidth,"allocated native viewport");
            near(box.contentWidth,Math.max(actual,box.availableWidth),"allocated public content width");
            near(box.implicitWidth,((box.alignment & Qt.AlignHorizontal_Mask) ? actual : box.implicitContentWidth)
                +box.leftPadding+box.rightPadding,"natural width is not stretched allocation");
            compare(box.currentIndex,row.currentIndex,"native index owners agree");
            compare(box.currentItem,row.currentItem,"native currentItem owners agree");
            if (!box.count) return;
            let first=Infinity,last=-Infinity;
            for (let i=0;i<box.count;++i) {
                const button=box.itemAt(i), point=button.mapToItem(box,0,0);
                first=Math.min(first,point.x); last=Math.max(last,point.x+button.width);
                verify(button.parent === row.contentItem,"real button belongs to the active native view");
            }
            near(first,left,"fresh actual button leading edge");
            near(last,left+actual,"fresh actual button trailing edge");
        }
        function test_alignment_data(): var {
            const rows=[];
            for (const alignment of [Qt.AlignRight,Qt.AlignLeft,Qt.AlignTop,Qt.AlignBottom,
                    Qt.AlignRight|Qt.AlignBottom,Qt.AlignHCenter,Qt.AlignRight|Qt.AlignAbsolute,
                    Qt.AlignLeft|Qt.AlignAbsolute,Qt.AlignRight|Qt.AlignHCenter,0])
                for (const mirror of [[false,false],[true,true],[true,false],[false,true]])
                    rows.push({tag:alignment+"-"+mirror.join("-"),alignment:alignment,root:mirror[0],row:mirror[1]});
            return rows;
        }
        function test_alignment(data): void {
            const box=createTemporaryObject(boxComponent,scene,{standardButtons:T.DialogButtonBox.Ok|T.DialogButtonBox.Cancel});
            verify(box !== null); frame(box);
            near(box.itemAt(0).height,30,"first-frame native button height");
            box.alignment=data.alignment;
            mirrors(box,data.root,data.row);
            box.leftPadding=13; box.rightPadding=27;
            assertGeometry(box);
            box.width=45; assertGeometry(box);
            box.width=340; assertGeometry(box);
            box.standardButtons=T.DialogButtonBox.NoButton;
            const custom=createTemporaryObject(customComponent,box,{width:150});
            verify(custom !== null); box.addItem(custom);
            for (const width of [340,100,45]) {
                box.width=width; assertGeometry(box);
                near(custom.width,150,"explicit custom width remains caller-owned");
            }
            box.removeItem(custom);
        }
        function test_modelAndKeyboard_data(): var { return [{tag:"keys",keys:true},{tag:"no-keys",keys:false}]; }
        function replaceStandardButtons(box,buttons): void {
            // Observe the public consumers, not helper counters: IA horizontal
            // geometry must not publish inside Qt's synchronous model mutation.
            let publications=0;
            const record=function() { ++publications; };
            const row=box.contentItem;
            box.implicitWidthChanged.connect(record);
            box.contentWidthChanged.connect(record);
            row.implicitWidthChanged.connect(record);
            try {
                box.standardButtons=buttons;
                compare(publications,0,"no horizontal publication inside model setter");
            } finally {
                box.implicitWidthChanged.disconnect(record);
                box.contentWidthChanged.disconnect(record);
                row.implicitWidthChanged.disconnect(record);
            }
        }
        function test_modelAndKeyboard(data): void {
            const box=createTemporaryObject(boxComponent,scene,{standardButtons:T.DialogButtonBox.Close});
            verify(box !== null); frame(box);
            compare(box.currentIndex,-1); compare(box.contentItem.currentIndex,-1);
            near(box.itemAt(0).height,30,"cold Close height");
            replaceStandardButtons(box,T.DialogButtonBox.Ok|T.DialogButtonBox.Cancel);
            assertGeometry(box);
            compare(box.currentIndex,0); compare(box.currentItem,box.itemAt(0));
            if (data.keys) {
                const first=box.itemAt(0),second=box.itemAt(1);
                let clicked=null,accepted=0,rejected=0;
                box.clicked.connect(function(button) { clicked=button; });
                box.accepted.connect(function() { ++accepted; });
                box.rejected.connect(function() { ++rejected; });
                verify(TestProbe.activate(first));
                tryCompare(TestProbe.hostWindow(first),"active",true);
                first.forceActiveFocus(Qt.TabFocusReason); tryCompare(first,"activeFocus",true);
                verify(TestProbe.keyClick(first,Qt.Key_Tab));
                tryCompare(second,"activeFocus",true);
                compare(box.currentIndex,0); compare(box.currentItem,first);
                verify(TestProbe.keyClick(second,Qt.Key_Space));
                compare(clicked,second);
                compare(accepted,second.T.DialogButtonBox.buttonRole === T.DialogButtonBox.AcceptRole ? 1 : 0);
                compare(rejected,second.T.DialogButtonBox.buttonRole === T.DialogButtonBox.RejectRole ? 1 : 0);
            }
            replaceStandardButtons(box,T.DialogButtonBox.Close);
            for (let i=0;i<2;++i) {
                assertGeometry(box);
                compare(box.currentIndex,-1); compare(box.contentItem.currentIndex,-1);
                compare(box.currentItem,null); compare(box.contentItem.currentItem,null);
                verify(!box.itemAt(0).focus);
            }
            box.standardButtons=T.DialogButtonBox.NoButton;
            assertGeometry(box); near(box.contentItem.height,0,"empty native viewport height");
            near(box.contentItem.y,box.height/2,"empty native viewport centered");
            const custom=createTemporaryObject(customComponent,box);
            verify(custom !== null); box.addItem(custom); assertGeometry(box);
            verify(!box.hasRejectAction);
            custom.T.DialogButtonBox.buttonRole=T.DialogButtonBox.RejectRole;
            verify(box.hasRejectAction);
            box.removeItem(custom); assertGeometry(box); verify(!box.hasRejectAction);
        }
        function test_nativeVerticalInvalidation(): void {
            const box=createTemporaryObject(boxComponent,scene,{standardButtons:T.DialogButtonBox.NoButton,
                leftPadding:12,rightPadding:12,topPadding:6,bottomPadding:6,spacing:8});
            const native=createTemporaryObject(nativeComponent,scene);
            verify(box !== null && native !== null);
            const pairs=[];
            for (const owner of [box,native]) {
                const short=createTemporaryObject(customComponent,owner,{text:"Short",implicitHeight:24});
                const tall=createTemporaryObject(customComponent,owner,{text:"Tall",implicitHeight:54});
                verify(short !== null && tall !== null); owner.addItem(short); owner.addItem(tall);
                pairs.push({short:short,tall:tall});
            }
            function compareNative() {
                frame(box);
                near(box.contentHeight,native.contentHeight,"native scalar height");
                near(box.implicitHeight,native.implicitHeight,"native implicit height");
                near(box.contentItem.y,native.contentItem.y,"actual original native y");
                near(box.contentItem.height,native.contentItem.height,"actual original native view height");
                for (let i=0;i<2;++i) near(box.itemAt(i).height,native.itemAt(i).height,"actual native button height");
                near(box.height,72,"explicit fixed box height");
            }
            compareNative();
            for (const p of pairs) { p.short.fontPixelSize=25; p.tall.fontPixelSize=25; }
            compareNative();
            for (const p of pairs) p.tall.implicitHeight=76;
            compareNative(); compareNative();
            for (const owner of [box,native]) { owner.removeItem(owner.itemAt(1)); owner.removeItem(owner.itemAt(0)); }
        }
        function test_renamedStandardButton_data(): var {
            const rows=[];
            for (const mode of [T.Popup.Item,T.Popup.Window])
                for (const rtl of [false,true])
                    for (const fixed of [false,true])
                        rows.push({tag:mode+"-"+rtl+"-"+fixed,mode:mode,rtl:rtl,fixed:fixed});
            return rows;
        }
        function test_renamedStandardButton(data): void {
            scene.LayoutMirroring.enabled=data.rtl;
            const dialog=createTemporaryObject(dialogComponent,scene,{parent:scene,popupType:data.mode,
                standardButtons:T.Dialog.Ok|T.Dialog.Cancel});
            verify(dialog !== null);
            // The component has completed and the native standard model exists
            // before the application assigns its actual action caption.
            const box=dialog.footer;
            let ok=box.standardButton(T.DialogButtonBox.Ok);
            verify(ok !== null);
            ok.text="Add source";
            if (data.fixed) dialog.width=340;
            dialog.open(); tryCompare(dialog,"opened",true);
            try {
                if (data.mode === T.Popup.Window)
                    verify(TestProbe.hostWindow(box) !== TestProbe.hostWindow(scene),"real Window hosting");
                for (const scale of [1,1.5,1]) {
                    IA.Appearance.input={fontScale:scale,reducedMotion:true};
                    for (const caption of ["Add source","Add selected source","Add","Add source"]) {
                        ok.text=caption;
                        assertGeometry(box);
                        near(ok.width,ok.implicitWidth,"renamed native button tracks its natural caption width");
                        if (!data.fixed) {
                            verify(dialog.width >= box.implicitWidth-0.5,"natural dialog grows with actual footer extent");
                            verify(extent(box) <= box.availableWidth+0.5,"natural footer contains its buttons");
                        } else near(dialog.width,340,"explicit dialog width stays caller-owned");
                        const before=ok.width;
                        box.fontPixelSize=box.fontPixelSize+2;
                        assertGeometry(box);
                        verify(ok.width >= before,"live local font reaches the actual standard button");
                        box.resetAppearance();
                    }
                }
                box.standardButtons=T.DialogButtonBox.Ok|T.DialogButtonBox.Cancel|T.DialogButtonBox.Apply;
                assertGeometry(box);
                // Qt rebuilds standard delegates when this bitmask changes.
                // Reapply the consumer caption to the new public standard button.
                ok=box.standardButton(T.DialogButtonBox.Ok);
                verify(ok !== null); ok.text="Add source"; assertGeometry(box);
                const custom=createTemporaryObject(customComponent,box,{text:"Custom action",width:120});
                verify(custom !== null); box.addItem(custom); assertGeometry(box);
                box.removeItem(custom); assertGeometry(box);
                compare(box.standardButton(T.DialogButtonBox.Ok),ok,"custom mutation keeps the native standard item");
                compare(ok.text,"Add source","custom mutation retains the renamed caption");
                box.standardButtons=T.DialogButtonBox.Ok|T.DialogButtonBox.Cancel;
                ok=box.standardButton(T.DialogButtonBox.Ok);
                verify(ok !== null); ok.text="Add source";
                assertGeometry(box);
                near(ok.width,ok.implicitWidth,"final rename remains naturally sized");
            } finally { dialog.close(); }
        }
        function test_dialogModes_data(): var {
            const rows=[];
            for (const mode of [T.Popup.Item,T.Popup.Window])
                for (const fixed of [false,true])
                    for (const rtl of [false,true]) rows.push({tag:mode+"-"+fixed+"-"+rtl,mode:mode,fixed:fixed,rtl:rtl});
            return rows;
        }
        function test_dialogModes(data): void {
            scene.LayoutMirroring.enabled=data.rtl;
            const properties={parent:scene,popupType:data.mode,standardButtons:T.Dialog.Close};
            if (data.fixed) properties.height=260;
            const dialog=createTemporaryObject(dialogComponent,scene,properties);
            verify(dialog !== null);
            try {
                dialog.open(); tryCompare(dialog,"opened",true); frame(dialog.footer);
                const host=TestProbe.hostWindow(dialog.contentItem);
                if (data.mode === T.Popup.Window) {
                    verify(host !== TestProbe.hostWindow(scene),"real Window host, not Item fallback");
                    near(host.width,dialog.width,"first Window width"); near(host.height,dialog.height,"first Window height");
                }
                near(dialog.footer.itemAt(0).height,30,"first popup frame native button height");
                for (const rtl of [false,true,false]) {
                    dialog.contentItem.parent.LayoutMirroring.enabled=rtl;
                    dialog.contentItem.parent.LayoutMirroring.childrenInherit=true;
                    assertGeometry(dialog.footer);
                    compare(dialog.footer.mirrored,rtl);
                }
                for (const buttons of [T.Dialog.Ok,T.Dialog.Cancel]) {
                    dialog.standardButtons=buttons;
                    if (!dialog.opened) { dialog.open(); tryCompare(dialog,"opened",true); }
                    assertGeometry(dialog.footer);
                    const close=findChild(dialog,"iaDialogClose");
                    verify(close !== null); compare(close.visible,buttons === T.Dialog.Cancel);
                    compare(dialog.footer.hasRejectAction,buttons === T.Dialog.Cancel);
                    let accepted=0,rejected=0;
                    const a=function() { ++accepted; },r=function() { ++rejected; };
                    dialog.accepted.connect(a); dialog.rejected.connect(r);
                    const button=dialog.footer.standardButton(buttons);
                    verify(TestProbe.activate(button));
                    verify(TestProbe.click(button,button.width/2,button.height/2));
                    compare(accepted,buttons === T.Dialog.Ok ? 1 : 0);
                    compare(rejected,buttons === T.Dialog.Cancel ? 1 : 0);
                    tryCompare(dialog,"visible",false);
                    dialog.accepted.disconnect(a); dialog.rejected.disconnect(r);
                }
            } finally { dialog.close(); }
        }
    }
}
