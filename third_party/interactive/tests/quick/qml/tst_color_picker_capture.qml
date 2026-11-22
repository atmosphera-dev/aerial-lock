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
    width: 900; height: 760
    property color applied: "#a649335f"
    property IAColorDialog dialog: null
    Component {
        id: pickerComponent
        IAColorDialog {
            title: "Accent color"
            parentWindow: scene.Window.window
            colorValue: scene.applied
            onColorEdited: value => { scene.applied=value; }
        }
    }
    TestCase {
        id: tests
        readonly property IAColorDialog dialog: scene.dialog
        name: "ColorPickerCapture"
        when: windowShown
        function visual(root, name): var {
            if(root.objectName===name) return root;
            for(const child of root.children) { const found=visual(child,name); if(found) return found; }
            return null;
        }
        function test_capture(): void {
            failOnWarning(/.*(TypeError|ReferenceError|Binding loop|polish loop|Unable to assign|Cannot anchor).*/);
            const manifest=[];
            for(const scheme of ["light","dark"]) {
                for(const contrast of ["normal","high"]) {
                    for(const variant of ["hex","rgb","hsv","hsl","focus","error","alpha","touch","font","narrow","rtl","amber","danger","drag","unapplied-draft","endpoint"]) {
                        IA.Appearance.input={scheme:scheme,contrast:contrast,density:variant==="touch"?"touch":"compact",
                            fontScale:variant==="font"?1.5:1,reducedMotion:true};
                        scene.applied=variant==="amber"?"#99739548":variant==="alpha"?"#4049335f":"#a649335f";
                        scene.dialog=createTemporaryObject(pickerComponent,scene);
                        verify(dialog!==null);
                        if(variant==="narrow") dialog.width=340;
                        scene.Window.window.contentItem.LayoutMirroring.enabled=variant==="rtl";
                        scene.Window.window.contentItem.LayoutMirroring.childrenInherit=true;
                        dialog.title=variant==="amber"?"Amber color":variant==="danger"?"Danger color":"Accent color";
                        dialog.open(); tryCompare(dialog,"opened",true);
                        tryVerify(function() { return TestProbe.exposed(dialog.contentItem); });
                        const stage="capture/fresh/"+scheme+"/"+contrast+"/"+variant,started=Date.now();
                        console.info("IA_PICKER_READINESS_BEGIN "+JSON.stringify({stage:stage,host:String(TestProbe.hostWindow(dialog.contentItem)),main:String(scene.Window.window)}));
                        const ready=TestProbe.activateForInput(dialog.contentItem);
                        const host=TestProbe.hostWindow(dialog.contentItem),main=scene.Window.window;
                        const payload={stage:stage,elapsedMs:Date.now()-started,ready:ready,
                            exposed:TestProbe.exposed(dialog.contentItem),active:host?host.active:false,
                            exactOwner:TestProbe.inputHostReady(dialog.contentItem),mainReady:TestProbe.inputHostReady(scene),
                            mainActive:main.active,sameHost:host===main,host:String(host),main:String(main),
                            geometry:host?[host.x,host.y,host.width,host.height]:[]};
                        console.info((ready?"IA_PICKER_READINESS_END ":"IA_PICKER_READINESS_FAIL ")+JSON.stringify(payload));
                        verify(ready,"Native picker input owner "+JSON.stringify(payload));
                        const modes=findChild(dialog,"iaColorModes"), plane=findChild(dialog,"iaColorPlane");
                        const selected=variant==="rgb"?1:variant==="hsv"?2:variant==="hsl"?3:0;
                        verify(TestProbe.click(modes.itemAt(selected),modes.itemAt(selected).width/2,modes.itemAt(selected).height/2));
                        verify(TestProbe.move(dialog.contentItem,10,dialog.contentItem.height+IA.Appearance.metrics.spacingS));
                        if(variant==="focus") plane.forceActiveFocus(Qt.TabFocusReason);
                        if(variant==="endpoint") verify(TestProbe.click(plane,plane.width-1,plane.height-1));
                        if(variant==="error") {
                            const field=visual(dialog.contentItem,"iaColor_hex");
                            verify(TestProbe.click(field,field.width/2,field.height/2));
                            verify(TestProbe.keyClick(field,Qt.Key_A,Qt.ControlModifier));
                            verify(TestProbe.typeAscii(field,"#invalid"));
                            verify(TestProbe.keyClick(field,Qt.Key_Return));
                        }
                        if(variant==="unapplied-draft") {
                            const field=visual(dialog.contentItem,"iaColor_hex");
                            verify(TestProbe.click(field,field.width/2,field.height/2));
                            verify(TestProbe.keyClick(field,Qt.Key_A,Qt.ControlModifier));
                            verify(TestProbe.typeAscii(field,"#12"));
                            scene.applied="#99739548";
                            compare(field.text,"#12");
                            compare(dialog.currentColor,scene.applied);
                        }
                        if(variant==="drag") verify(TestProbe.press(plane,plane.width*0.6,plane.height*0.3));
                        verify(TestProbe.render(dialog.contentItem));
                        const name=scheme+"-"+contrast+"-"+variant+".png";
                        verify(TestProbe.captureWindow(dialog.contentItem,name));
                        manifest.push({name:name,title:dialog.title,width:dialog.width,height:dialog.height,
                            plane:[plane.width,plane.height],host:[TestProbe.hostWindow(dialog.contentItem).width,TestProbe.hostWindow(dialog.contentItem).height],
                            argb:findChild(dialog,"iaColorArgb").text});
                        if(variant==="drag") verify(TestProbe.release(plane,plane.width*0.6,plane.height*0.3));
                        dialog.close(); tryCompare(dialog,"visible",false);
                        dialog.destroy();
                        scene.dialog=null;
                    }
                }
            }
            verify(TestProbe.writeCaptureManifest(JSON.stringify(manifest,null,2)));
            console.info("IA_COLOR_PICKER_CAPTURE frames="+manifest.length);
        }
    }
}
