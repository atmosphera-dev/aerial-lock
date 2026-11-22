// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QtTest
import InterActive
import InterActive.Foundation as IA
import InterActive.Tests

Item {
    id: scene
    width: 640; height: 480
    property color selected: "#ff7a18"
    property int selections: 0
    property int defaults: 0
    property int customs: 0
    property bool closedAtCustom: false
    IAColorWell {
        id: well
        x: 40; y: 40
        label: "Amber"
        colorValue: scene.selected
        presets: [{name:"Blue",hex:"#245bdb"},{name:"Red",hex:"#8f263b"}]
        onColorSelected: value => { ++scene.selections; }
        onDefaultRequested: ++scene.defaults
        onCustomColorRequested: {
            ++scene.customs;
            scene.closedAtCustom = !quickPalette.visible;
        }
    }
    IAColorWell { id: plain; x: 160; y: 40; customColors: false }
    readonly property T.AbstractButton sampleButton: tests.findChild(well,"iaColorWellSampleButton") as T.AbstractButton
    readonly property T.AbstractButton customButton: tests.findChild(well,"iaColorWellCustomButton") as T.AbstractButton
    readonly property Rectangle sample: tests.findChild(well,"iaColorWellSample") as Rectangle
    readonly property Image rainbow: tests.findChild(well,"iaColorWellRainbow") as Image
    readonly property T.Menu quickPalette: tests.findChild(well,"iaColorWellMenu") as T.Menu

    TestCase {
        id: tests
        name: "ColorWell"
        when: windowShown
        function init(): void {
            IA.Appearance.input = {scheme:"light",contrast:"normal",reducedMotion:true};
            scene.selected="#ff7a18";
            scene.selections=0; scene.defaults=0; scene.customs=0; scene.closedAtCustom=false;
            well.LayoutMirroring.enabled=false;
            well.LayoutMirroring.childrenInherit=true;
            well.customColors=true;
            well.presets=[{name:"Blue",hex:"#245bdb"},{name:"Red",hex:"#8f263b"}];
            quickPalette.close();
            verify(TestProbe.activateForInput(well));
            verify(TestProbe.move(scene,400,350));
            failOnWarning(/.*(TypeError|ReferenceError|Binding loop|polish loop|Unable to assign).*/);
        }
        function cleanup(): void { quickPalette.close(); }
        function click(item,x,y): void {
            verify(TestProbe.click(item,x === undefined ? item.width/2 : x,y === undefined ? item.height/2 : y));
        }
        function geometry(): var {
            return [sample.x,sample.y,sample.width,sample.height,rainbow.x,rainbow.y,rainbow.width,rainbow.height];
        }
        function assertGeometry(): void {
            compare(well.width,plain.width); compare(well.height,plain.height);
            compare(well.width,2*well.height);
            compare(sample.width,well.width-2*IA.Appearance.metrics.spacingXS);
            compare(sample.height,well.height-2*IA.Appearance.metrics.spacingXS);
            compare(customButton.height,sample.height); compare(customButton.width,sample.height);
            const s=sample.mapToItem(well,0,0), c=customButton.mapToItem(well,0,0);
            fuzzyCompare(s.y,c.y,0.01); fuzzyCompare(s.y+sample.height,c.y+customButton.height,0.01);
            fuzzyCompare(well.mirrored ? c.x : c.x+customButton.width,
                well.mirrored ? s.x : s.x+sample.width,0.01);
            fuzzyCompare(s.x,well.width-s.x-sample.width,0.01);
            fuzzyCompare(s.y,well.height-s.y-sample.height,0.01);
            compare(rainbow.width,customButton.width-2*IA.Appearance.metrics.borderWidth);
            compare(rainbow.height,rainbow.width);
            compare(sample.color,scene.selected);
        }
        function test_geometry_data(): var {
            const rows=[];
            for (const scheme of ["light","dark"])
                for (const contrast of ["normal","high"])
                    for (const touch of [false,true])
                        for (const rtl of [false,true])
                            rows.push({tag:scheme+contrast+touch+rtl,scheme:scheme,contrast:contrast,touch:touch,rtl:rtl});
            return rows;
        }
        function test_geometry(data): void {
            IA.Appearance.input={scheme:data.scheme,contrast:data.contrast,density:data.touch?"touch":"compact",
                fontScale:1.5,reducedMotion:true};
            well.LayoutMirroring.enabled=data.rtl;
            verify(TestProbe.render(well)); assertGeometry();
            if(data.touch) { verify(well.height>=44); compare(customButton.height,well.height-2*IA.Appearance.metrics.spacingXS); }
            const before=geometry();
            sampleButton.forceActiveFocus(Qt.TabFocusReason);
            verify(TestProbe.move(sampleButton,data.rtl?sampleButton.width-8:8,sampleButton.height/2));
            verify(TestProbe.render(well)); compare(geometry(),before);
            verify(TestProbe.press(sampleButton,data.rtl?sampleButton.width-8:8,sampleButton.height/2));
            compare(sampleButton.down,true); compare(geometry(),before);
            verify(TestProbe.release(sampleButton,data.rtl?sampleButton.width-8:8,sampleButton.height/2)); quickPalette.close();
            customButton.forceActiveFocus(Qt.TabFocusReason);
            verify(TestProbe.move(customButton,customButton.width/2,customButton.height/2));
            tryCompare(customButton,"hovered",true); compare(sampleButton.hovered,false);
            compare(geometry(),before); compare(sample.color,scene.selected);
            verify(TestProbe.press(customButton,customButton.width/2,customButton.height/2));
            compare(customButton.down,true); compare(sampleButton.down,false); compare(geometry(),before);
            verify(TestProbe.release(customButton,customButton.width/2,customButton.height/2));
            compare(geometry(),before);
        }
        function test_targetsAndMenu(): void {
            compare(well.focusPolicy,Qt.NoFocus);
            compare(well.activeFocusOnTab,false);
            compare(TestProbe.accessibleState(sampleButton).name,"Amber presets");
            compare(TestProbe.accessibleState(customButton).name,"Choose custom amber color");
            compare(sampleButton.parent,customButton.parent);
            verify(customButton.contains(Qt.point(customButton.width/2,customButton.height/2)));
            verify(!customButton.contains(Qt.point(1,1)));
            // One real corner click reaches the lower native sample button.
            click(customButton,1,1); tryCompare(quickPalette,"opened",true); compare(scene.customs,0);
            compare(quickPalette.count,4); verify(!(quickPalette.itemAt(0) instanceof T.AbstractButton));
            const preset=quickPalette.itemAt(1);
            click(preset); tryCompare(quickPalette,"visible",false); compare(scene.selections,1);
            compare(well.colorValue,"#ff7a18");
            scene.selected="#73947b"; compare(sample.color,"#73947b");
            click(sampleButton,8,sampleButton.height/2); tryCompare(quickPalette,"opened",true);
            click(quickPalette.itemAt(3)); tryCompare(quickPalette,"visible",false); compare(scene.defaults,1);
            well.presets=[];
            click(sampleButton,8,sampleButton.height/2); tryCompare(quickPalette,"opened",true); compare(quickPalette.count,2);
            // Existing Item-popup behavior must allow the actual outside custom target.
            click(customButton); compare(scene.customs,1); compare(scene.closedAtCustom,true); compare(quickPalette.visible,false);
            well.customColors=false;
            compare(customButton.visible,false); compare(customButton.Accessible.ignored,true);
            click(sampleButton); tryCompare(quickPalette,"opened",true); compare(scene.customs,1);
        }
        function test_circleCorners_data(): var { return [{tag:"ltr",rtl:false},{tag:"rtl",rtl:true}]; }
        function test_circleCorners(data): void {
            well.LayoutMirroring.enabled=data.rtl;
            verify(TestProbe.render(well));
            for(const point of [Qt.point(1,1),Qt.point(customButton.width-1,1),
                    Qt.point(1,customButton.height-1),Qt.point(customButton.width-1,customButton.height-1)]) {
                verify(!customButton.contains(point));
                click(customButton,point.x,point.y);
                tryCompare(quickPalette,"opened",true); compare(scene.customs,0);
                quickPalette.close(); tryCompare(quickPalette,"visible",false);
            }
        }
        function dispatchSpace(target: Item): void {
            if (TestProbe.isWayland()) {
                verify(TestProbe.nativeKeyHostReady(target));
                verify(TestProbe.nativeKeyClick(target,Qt.Key_Space));
            } else {
                verify(TestProbe.keyClick(target,Qt.Key_Space));
            }
        }
        function test_nativeKeyboard(): void {
            verify(TestProbe.move(scene,400,350));
            sampleButton.forceActiveFocus(Qt.TabFocusReason);
            dispatchSpace(sampleButton);
            tryCompare(quickPalette,"opened",true);
            verify(TestProbe.keyClick(quickPalette.contentItem,Qt.Key_Down));
            tryCompare(quickPalette,"currentIndex",1);
            quickPalette.close();
            customButton.forceActiveFocus(Qt.TabFocusReason);
            dispatchSpace(customButton);
            compare(scene.customs,1); compare(rainbow.rotation,0,"Focus alone does not spin");
            sampleButton.forceActiveFocus(Qt.TabFocusReason);
            verify(TestProbe.keyClick(sampleButton,Qt.Key_Tab)); compare(customButton.activeFocus,true);
            well.customColors=false;
            sampleButton.forceActiveFocus(Qt.TabFocusReason);
            verify(TestProbe.keyClick(sampleButton,Qt.Key_Tab)); verify(!customButton.activeFocus);
        }
        function test_motion(): void {
            const sampleButton=scene.sampleButton, customButton=scene.customButton, rainbow=scene.rainbow;
            IA.Appearance.input={reducedMotion:false,tokens:{motion:{feedbackDuration:600}}};
            compare(IA.Appearance.motion.feedbackDuration,600);
            const animation=findChild(well,"iaColorWellRotation");
            const rim=findChild(well,"iaColorWellRim") as Rectangle;
            const initial=geometry(), rimColor=rim.border.color;
            verify(TestProbe.move(sampleButton,8,sampleButton.height/2)); compare(rainbow.rotation,0);
            verify(TestProbe.move(customButton,customButton.width/2,customButton.height/2));
            tryVerify(function(){return rainbow.rotation>10 && rainbow.rotation<170;});
            const forward=rainbow.rotation;
            verify(TestProbe.move(scene,400,350));
            verify(Math.abs(rainbow.rotation-forward)<20,"Retarget from current angle, no jump");
            tryVerify(function(){return rainbow.rotation<forward;});
            const returning=rainbow.rotation;
            verify(TestProbe.move(customButton,customButton.width/2,customButton.height/2));
            verify(Math.abs(rainbow.rotation-returning)<20);
            tryCompare(rainbow,"rotation",180); compare(geometry(),initial); compare(rim.border.color,rimColor);
            verify(TestProbe.move(scene,400,350)); tryCompare(rainbow,"rotation",0);
            const receipts=[];
            for(const direction of ["forward","return"]) {
                verify(TestProbe.move(customButton,customButton.width/2,customButton.height/2));
                if(direction==="return") {
                    tryCompare(rainbow,"rotation",180);
                    verify(TestProbe.move(scene,400,350));
                }
                tryVerify(function(){return rainbow.rotation>5 && rainbow.rotation<175;});
                const before=rainbow.rotation;
                IA.Appearance.input={reducedMotion:true,tokens:{motion:{feedbackDuration:600}}};
                compare(rainbow.rotation,0); compare(animation.running,false);
                wait(30); compare(rainbow.rotation,0); compare(animation.running,false);
                receipts.push({direction:direction,before:before,after:rainbow.rotation,running:animation.running});
                IA.Appearance.input={reducedMotion:false,tokens:{motion:{feedbackDuration:600}}};
                if(customButton.hovered) tryCompare(rainbow,"rotation",180);
                verify(TestProbe.move(scene,400,350)); tryCompare(rainbow,"rotation",0);
            }
            console.info("IA_COLOR_WELL_MOTION "+JSON.stringify({duration:600,forward:forward,returning:returning,cancellations:receipts}));
        }
    }
}
