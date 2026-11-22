// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QtTest
import InterActive
import InterActive.Foundation as IA
import InterActive.Tests

Rectangle {
    id: scene
    anchors.fill: parent
    implicitWidth: 640; implicitHeight: 360
    color: IA.Appearance.colors.canvas
    IALabel { x:32; y:24; text:"Color wells"; fontWeight:IA.Appearance.typography.emphasisWeight }
    IALabel { id:caption; x:32; y:64 }
    Repeater {
        id: wells
        model: 4
        delegate: Item {
            id: cell
            required property int index
            x:32+(index%2)*280; y:112+Math.floor(index/2)*92
            width:240; height:72
            IALabel { text:(cell.index<2?"Amber":"Near white")+(cell.index%2?" - custom":" - presets"); anchors.left:parent.left }
            IAColorWell {
                objectName:"captureWell"
                y:28
                label:cell.index<2?"Amber":"Near white"
                colorValue:cell.index<2?"#ff7a18":"#f5f5ef"
                customColors:cell.index%2===1
                presets:[{name:"Blue",hex:"#245bdb"}]
            }
        }
    }
    TestCase {
        name:"ColorWellCapture"
        when:windowShown
        property string manifest:""
        property int count:0
        function initTestCase(): void {
            verify(TestProbe.captureDirectory().length>0);
            failOnWarning(/.*(TypeError|ReferenceError|Binding loop|polish loop).*/);
        }
        function init(): void {
            failOnWarning(/.*(TypeError|ReferenceError|Binding loop|polish loop).*/);
        }
        function cleanupTestCase(): void { verify(TestProbe.writeCaptureManifest(manifest)); compare(count,28); }
        function test_states_data(): var {
            const rows=[];
            for(const scheme of ["light","dark"])
                for(const contrast of ["normal","high"])
                    for(const state of ["rest","sample-hover","sample-focus","sample-press","circle-hover","circle-focus","circle-press"])
                        rows.push({tag:scheme+"-"+contrast+"-"+state,scheme:scheme,contrast:contrast,state:state});
            return rows;
        }
        function test_states(data): void {
            IA.Appearance.input={scheme:data.scheme,contrast:data.contrast,reducedMotion:false};
            caption.text=data.scheme+" / "+data.contrast+" / "+data.state;
            verify(TestProbe.activateForInput(scene));
            verify(TestProbe.move(scene,600,330));
            const targetWell=findChild(wells.itemAt(1),"captureWell") as IAColorWell;
            const target=findChild(targetWell,data.state.startsWith("circle")?"iaColorWellCustomButton":"iaColorWellSampleButton") as T.AbstractButton;
            const x=data.state.startsWith("circle")?target.width/2:8, y=target.height/2;
            if(data.state.endsWith("focus")) target.forceActiveFocus(Qt.TabFocusReason);
            else scene.forceActiveFocus(Qt.OtherFocusReason);
            if(data.state.endsWith("hover") || data.state.endsWith("press")) {
                verify(TestProbe.move(target,x,y)); tryCompare(target,"hovered",true);
            }
            const pressed=data.state.endsWith("press");
            if(pressed) { verify(TestProbe.press(target,x,y)); compare(target.down,true); }
            try {
                const rainbow=findChild(targetWell,"iaColorWellRainbow") as Image;
                tryCompare(rainbow,"rotation",data.state.startsWith("circle") && !data.state.endsWith("focus")?180:0);
                verify(TestProbe.render(scene));
                const host=TestProbe.hostWindow(scene), nativeRoot=host.contentItem;
                compare(nativeRoot.x,0); compare(nativeRoot.y,0);
                compare(nativeRoot.width,host.width); compare(nativeRoot.height,host.height);
                const image=grabImage(nativeRoot), dpr=scene.Screen.devicePixelRatio;
                compare(image.width,Math.round(host.width*dpr)); compare(image.height,Math.round(host.height*dpr));
                const file="color-well-"+data.tag+".png";
                image.save(TestProbe.captureDirectory()+"/"+file);
                const regions=[];
                for(let i=0;i<4;++i) {
                    const well=findChild(wells.itemAt(i),"captureWell") as IAColorWell;
                    for(const name of ["iaColorWellSampleButton","iaColorWellSample","iaColorWellCustomButton","iaColorWellRainbow","iaColorWellRim"]) {
                        const item=findChild(well,name) as Item, p=item.mapToItem(nativeRoot,0,0);
                        regions.push({cell:i,name:name,x:p.x,y:p.y,width:item.width,height:item.height,visible:item.visible});
                    }
                }
                const receipt={file:file,width:host.width,height:host.height,dpr:dpr,scheme:data.scheme,contrast:data.contrast,
                    state:data.state,hovered:target.hovered,down:target.down,focus:target.visualFocus,rotation:rainbow.rotation,regions:regions};
                manifest+=JSON.stringify(receipt)+"\n"; ++count;
                console.info("IA_COLOR_WELL_CAPTURE "+JSON.stringify(receipt));
            } finally {
                if(pressed) verify(TestProbe.release(target,x,y));
                (findChild(targetWell,"iaColorWellMenu") as T.Menu).close();
                verify(TestProbe.move(scene,600,330));
            }
        }
    }
}
