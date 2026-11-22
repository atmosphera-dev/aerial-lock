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
    property color applied: Qt.rgba(0.23456, 0.34567, 0.45678, 0.65432)
    property int edits: 0
    property int writerPolicy: 0
    IAButton { id: opener; x: 30; y: 30; text: "Choose color"; onClicked: dialog.open() }
    property IAColorDialog dialog: null
    property IAColorDialog peer: null
    Component { id: directSurfaceComponent; IA.ColorPlaneSurface {} }
    Component {
        id: pickerComponent
        IAColorDialog {
            title: "Accent color"
            parentWindow: scene.Window.window
            colorValue: scene.applied
            onColorEdited: value => {
                ++scene.edits;
                if(scene.writerPolicy===0) scene.applied=value;
                else if(scene.writerPolicy===2) scene.applied="#654321";
            }
        }
    }
    Component {
        id: peerComponent
        IAColorDialog {
            title: "Amber color"
            parentWindow: scene.Window.window
            colorValue: "#99739548"
        }
    }
    TestCase {
        id: tests
        readonly property IAColorDialog dialog: scene.dialog
        readonly property IAColorDialog peer: scene.peer
        name: "ColorPicker"
        when: windowShown
        function visual(root, name): var {
            if(root.objectName===name) return root;
            for(const child of root.children) { const found=visual(child,name); if(found) return found; }
            return null;
        }
        function item(name): var { const value = findChild(dialog, name) || visual(dialog.contentItem,name); verify(value !== null, name); return value; }
        function click(target): void { verify(TestProbe.click(target, target.width / 2, target.height / 2)); }
        function activatePicker(stage): void {
            const started=Date.now();
            console.info("IA_PICKER_READINESS_BEGIN "+JSON.stringify({stage:stage,host:String(TestProbe.hostWindow(dialog.contentItem)),main:String(scene.Window.window)}));
            const ready=TestProbe.activateForInput(dialog.contentItem);
            const host=TestProbe.hostWindow(dialog.contentItem), main=scene.Window.window;
            const payload={stage:stage,elapsedMs:Date.now()-started,ready:ready,
                exposed:TestProbe.exposed(dialog.contentItem),active:host?host.active:false,
                exactOwner:TestProbe.inputHostReady(dialog.contentItem),mainReady:TestProbe.inputHostReady(scene),
                mainActive:main.active,sameHost:host===main,host:String(host),main:String(main),
                geometry:host?[host.x,host.y,host.width,host.height]:[],color:String(dialog.currentColor)};
            console.info((ready?"IA_PICKER_READINESS_END ":"IA_PICKER_READINESS_FAIL ")+JSON.stringify(payload));
            verify(ready,"Native picker input owner "+JSON.stringify(payload));
        }
        function open(): void {
            click(opener);
            tryCompare(dialog, "opened", true);
            tryVerify(function() { return TestProbe.exposed(dialog.contentItem); });
            activatePicker("open/fresh-click");
            verify(TestProbe.render(dialog.contentItem));
        }
        function reopen(stage): void {
            dialog.open(); tryCompare(dialog,"opened",true);
            tryVerify(function(){return TestProbe.exposed(dialog.contentItem);});
            activatePicker(stage);
        }
        function mode(index): void { click(item("iaColorModes").itemAt(index)); }
        function type(field, text): void {
            click(field);
            verify(TestProbe.keyClick(field, Qt.Key_A, Qt.ControlModifier));
            verify(TestProbe.typeAscii(field, text));
        }
        function commit(field): void { verify(TestProbe.keyClick(field, Qt.Key_Return)); }
        function init(): void {
            IA.Appearance.input = {reducedMotion:true};
            scene.applied = Qt.rgba(0.23456, 0.34567, 0.45678, 0.65432);
            scene.edits = 0;
            scene.writerPolicy = 0;
            failOnWarning(/.*(TypeError|ReferenceError|Binding loop|polish loop|Unable to assign|Cannot anchor).*/);
            scene.dialog=createTemporaryObject(pickerComponent,scene);
            scene.peer=createTemporaryObject(peerComponent,scene);
            verify(scene.dialog!==null && scene.peer!==null);
            dialog.close();
            peer.close();
            dialog.width = Qt.binding(function() {
                return Math.min(dialog.implicitWidth,TestProbe.screenAvailableGeometry(scene).width-2*dialog.margins);
            });
            scene.Window.window.contentItem.LayoutMirroring.enabled = false;
            scene.Window.window.contentItem.LayoutMirroring.childrenInherit = true;
            verify(TestProbe.activateForInput(opener));
        }
        function cleanup(): void { dialog.close(); peer.close(); }
        function test_authorityAndCleanBlur(): void {
            open(); mode(1);
            const before=Qt.rgba(scene.applied.r,scene.applied.g,scene.applied.b,scene.applied.a), count=scene.edits;
            click(item("iaColor_rgbRed")); click(item("iaColor_rgbGreen"));
            compare(scene.applied,before); compare(scene.edits,count,"Clean formatted text is not an edit");
            mode(0);
            const field=item("iaColor_hex");
            scene.writerPolicy=1;
            type(field,"#123456"); commit(field);
            compare(scene.applied,before); compare(dialog.currentColor,before);
            compare(item("iaColorPreviewSample").color,before); compare(field.text,"#123456");
            compare(field.Accessible.description,"Unapplied draft");
            scene.writerPolicy=2;
            commit(field);
            compare(scene.applied,"#654321"); compare(dialog.currentColor,"#654321");
            compare(field.text,"#123456");
            scene.writerPolicy=0;
            commit(field);
            compare(scene.applied,"#123456"); compare(dialog.currentColor,"#123456");
            compare(field.Accessible.description,"Applied color channel");
        }
        function test_nonmodalPeerIdentity(): void {
            open();
            const editor=item("iaColorEditor"), host=TestProbe.hostWindow(dialog.contentItem);
            peer.open(); tryCompare(peer,"opened",true);
            tryVerify(function(){return TestProbe.exposed(peer.contentItem);});
            verify(TestProbe.click(opener,opener.width/2,opener.height/2));
            compare(dialog.visible,true); compare(peer.visible,true);
            compare(item("iaColorEditor"),editor); compare(TestProbe.hostWindow(dialog.contentItem),host);
            verify(TestProbe.click(peer.contentItem,4,4));
            compare(dialog.visible,true); compare(peer.visible,true);
            compare(dialog.closePolicy,T.Popup.CloseOnEscape);
        }
        function test_openAndModePrecision(): void {
            const before = Qt.rgba(scene.applied.r, scene.applied.g, scene.applied.b, scene.applied.a);
            open();
            compare(dialog.modal, false); compare(dialog.popupType, T.Popup.Window);
            verify(TestProbe.hostWindow(dialog.contentItem) !== scene.Window.window);
            for (let index = 0; index < 4; ++index) mode(index);
            compare(scene.edits, 0); compare(dialog.currentColor, before); compare(scene.applied, before);
            const editor = item("iaColorEditor"), plane = item("iaColorPlane");
            IA.Appearance.input = {scheme:"dark",density:"touch",fontScale:1.2,reducedMotion:true};
            verify(TestProbe.render(dialog.contentItem));
            compare(item("iaColorEditor"), editor); compare(item("iaColorPlane"), plane);
            compare(scene.edits, 0); compare(dialog.currentColor, before);
            dialog.close(); reopen("mode-precision/reopen");
            compare(item("iaColorModes").currentIndex, 3); compare(scene.edits, 0);
        }
        function test_fieldsAndDrafts(): void {
            open(); mode(0);
            const field = item("iaColor_hex");
            type(field,"#80123456"); commit(field);
            compare(scene.applied,"#80123456");
            type(field,"#654321"); commit(field); compare(scene.applied,"#654321"); compare(scene.applied.a,1);
            mode(1);
            type(item("iaColor_rgbAlpha"),"37.5"); commit(item("iaColor_rgbAlpha"));
            fuzzyCompare(scene.applied.a,0.375,0.00002);
            const before = Qt.rgba(scene.applied.r,scene.applied.g,scene.applied.b,scene.applied.a);
            type(item("iaColor_rgbRed"),"256"); commit(item("iaColor_rgbRed"));
            compare(scene.applied,before); compare(item("iaColor_rgbRed").validation,IA.Presentation.Invalid);
            compare(item("iaColor_rgbRed").text,"256");
            mode(2); mode(1); compare(item("iaColor_rgbRed").text,"256");
            scene.applied="#548192";
            compare(item("iaColor_rgbRed").text,"256"); compare(dialog.currentColor,"#548192");
            type(item("iaColor_rgbRed"),"73"); commit(item("iaColor_rgbRed"));
            compare(scene.applied.r,73/255); compare(item("iaColor_rgbRed").validation,IA.Presentation.None);
            for (const entry of [{mode:2,key:"hsvHue",value:"360"},{mode:2,key:"hsvSaturation",value:"78.25"},
                    {mode:2,key:"hsvValue",value:"61.25"},{mode:3,key:"hslHue",value:"120.5"},
                    {mode:3,key:"hslSaturation",value:"66.25"},{mode:3,key:"hslLightness",value:"44.25"}]) {
                mode(entry.mode); const target=item("iaColor_"+entry.key);
                type(target,entry.value); commit(target); compare(target.validation,IA.Presentation.None);
                compare(dialog.currentColor,scene.applied);
            }
            compare(item("iaColorPreviewSample").color,scene.applied);
            compare(item("iaColorArgb").text.length,9);
        }
        function test_planeAndCache(): void {
            open();
            const plane=item("iaColorPlane"), surface=item("iaColorPlaneSurface");
            const initial=TestProbe.colorPlaneRebuilds(surface);
            verify(initial>0);
            const alpha=scene.applied.a;
            const firstLight=(plane.width*0.2-plane.leftPadding)/plane.availableWidth;
            const firstSaturation=1-(plane.height*0.2-plane.topPadding)/plane.availableHeight;
            const dragLight=(plane.width*0.6-plane.leftPadding)/plane.availableWidth;
            const dragSaturation=1-(plane.height*0.7-plane.topPadding)/plane.availableHeight;
            verify(TestProbe.move(plane,plane.width*0.2,plane.height*0.2));
            verify(TestProbe.press(plane,plane.width*0.2,plane.height*0.2));
            try {
                tryCompare(plane,"pressed",true);
                fuzzyCompare(plane.lightness,firstLight,0.005); fuzzyCompare(plane.saturation,firstSaturation,0.005);
                verify(scene.edits>0); compare(dialog.currentColor,scene.applied);
                verify(TestProbe.move(plane,plane.width*0.6,plane.height*0.7));
                fuzzyCompare(plane.lightness,dragLight,0.005); fuzzyCompare(plane.saturation,dragSaturation,0.005);
                compare(plane.pressed,true); compare(scene.applied.a,alpha);
            } finally {
                verify(TestProbe.release(plane,plane.width*0.6,plane.height*0.7));
            }
            tryCompare(plane,"pressed",false);
            compare(TestProbe.colorPlaneRebuilds(surface),initial);
            plane.forceActiveFocus(Qt.TabFocusReason);
            verify(TestProbe.keyClick(plane,Qt.Key_Right));
            verify(TestProbe.keyClick(plane,Qt.Key_Up,Qt.ShiftModifier));
            fuzzyCompare(plane.lightness,dragLight+0.01,0.005); fuzzyCompare(plane.saturation,dragSaturation+0.1,0.005);
            compare(TestProbe.colorPlaneRebuilds(surface),initial);
            for(const point of [Qt.point(0,0),Qt.point(1,0),Qt.point(0,1),Qt.point(1,1)]) {
                const x=Math.min(plane.width-1,point.x*plane.width),y=Math.min(plane.height-1,point.y*plane.height);
                verify(TestProbe.click(plane,x,y));
                const expectedLight=Math.max(0,Math.min(1,(x-plane.leftPadding)/plane.availableWidth));
                const expectedSaturation=Math.max(0,Math.min(1,1-(y-plane.topPadding)/plane.availableHeight));
                fuzzyCompare(plane.lightness,expectedLight,0.005); fuzzyCompare(plane.saturation,expectedSaturation,0.005);
            }
            const hue=item("hueSlider"), opacity=item("alphaSlider");
            click(hue); verify(TestProbe.render(plane));
            verify(TestProbe.colorPlaneRebuilds(surface)>initial);
            const afterHue=TestProbe.colorPlaneRebuilds(surface);
            console.info("IA_COLOR_PLANE_CACHE "+JSON.stringify(TestProbe.colorPlaneCache(surface)));
            click(opacity); verify(TestProbe.render(plane));
            compare(TestProbe.colorPlaneRebuilds(surface),afterHue);
            wait(80); compare(TestProbe.colorPlaneRebuilds(surface),afterHue);
            dialog.width=340; verify(TestProbe.render(plane));
            // Narrow width retains the preferred square until the actual body is smaller.
            dialog.width=250; verify(TestProbe.render(plane));
            verify(TestProbe.colorPlaneRebuilds(surface)>afterHue);
            console.info("IA_COLOR_PLANE_CACHE_NARROW "+JSON.stringify(TestProbe.colorPlaneCache(surface)));
        }
        function test_accessibleAndKeys(): void {
            open(); mode(3);
            const plane=item("iaColorPlane"), hue=item("hueSlider"), opacity=item("alphaSlider");
            const group=TestProbe.accessibleState(plane);
            compare(group.role,Accessible.Grouping); compare(group.name,"Saturation and lightness");
            compare(group.focusable,true); verify(group.description.indexOf("Saturation")>=0);
            for(const slider of [hue,opacity]) {
                const state=TestProbe.accessibleState(slider);
                compare(state.role,Accessible.Slider); compare(state.minimum,slider.from); compare(state.maximum,slider.to);
                verify(state.actions.length>0);
                const before=slider.value;
                verify(TestProbe.accessibleAction(slider,state.increaseAction));
                verify(slider.value>=before);
            }
            const field=TestProbe.accessibleState(item("iaColor_hslLightness"));
            compare(field.role,Accessible.EditableText); compare(field.readOnly,false); compare(field.textInterface,true); verify(field.name.indexOf("HSL")>=0);
            const segment=item("iaColorModes").itemAt(3), segmentState=TestProbe.accessibleState(segment);
            compare(segmentState.role,Accessible.RadioButton); compare(segmentState.checked,true);
            plane.forceActiveFocus(Qt.TabFocusReason);
            verify(TestProbe.keyClick(plane,Qt.Key_Tab)); verify(!plane.activeFocus);
            verify(TestProbe.keyClick(dialog.contentItem,Qt.Key_Escape)); tryCompare(dialog,"visible",false);
            tryCompare(opener,"activeFocus",true);
        }
        function test_touchOutsideAndCancel(): void {
            dialog.width=340;
            IA.Appearance.input={fontScale:1.5,density:"touch",reducedMotion:true};
            open();
            const plane=item("iaColorPlane"), top=plane.mapToItem(null,0,0).y;
            verify(TestProbe.touch(plane,"press",plane.width*0.3,plane.height*0.3));
            try {
                tryCompare(plane,"pressed",true);
                verify(TestProbe.touch(plane,"move",-20,plane.height+20));
                tryCompare(plane,"lightness",0); tryCompare(plane,"saturation",0);
                compare(plane.pressed,true); fuzzyCompare(plane.mapToItem(null,0,0).y,top,0.1);
                compare(dialog.currentColor,scene.applied);
                verify(TestProbe.touch(plane,"cancel",-20,plane.height+20));
                tryCompare(plane,"pressed",false);
            } finally { verify(TestProbe.touch(plane,"release",-20,plane.height+20)); }
            verify(TestProbe.touch(plane,"press",plane.width*0.7,plane.height*0.4));
            tryCompare(plane,"pressed",true);
            verify(TestProbe.touch(plane,"release",plane.width*0.7,plane.height*0.4));
            tryCompare(plane,"pressed",false);
            compare(dialog.currentColor,scene.applied);
        }
        function test_geometry_data(): var {
            const rows=[];
            for(const scheme of ["light","dark"])
                for(const contrast of ["normal","high"])
                    for(const variant of ["compact","touch","font","narrow","rtl"])
                        rows.push({tag:scheme+"-"+contrast+"-"+variant,scheme:scheme,contrast:contrast,variant:variant});
            return rows;
        }
        function test_planeBoundary_data(): var {
            const rows=[];
            for(const scheme of ["light","dark"])
                for(const contrast of ["normal","high"])
                    for(const layout of ["ltr","rtl","narrow"])
                        rows.push({tag:scheme+"-"+contrast+"-"+layout,scheme:scheme,contrast:contrast,layout:layout});
            return rows;
        }
        function test_planeBoundary(data): void {
            IA.Appearance.input={scheme:data.scheme,contrast:data.contrast,reducedMotion:true};
            scene.Window.window.contentItem.LayoutMirroring.enabled=data.layout==="rtl";
            if(data.layout==="narrow") dialog.width=340;
            open();
            const plane=item("iaColorPlane"),surface=item("iaColorPlaneSurface");
            const geometry=[surface.x,surface.y,surface.width,surface.height];
            const rebuilds=TestProbe.colorPlaneRebuilds(surface),color=dialog.currentColor;
            plane.forceActiveFocus(Qt.TabFocusReason);
            verify(TestProbe.render(plane));
            compare(plane.visualFocus,true);
            compare([surface.x,surface.y,surface.width,surface.height],geometry);
            compare(TestProbe.colorPlaneRebuilds(surface),rebuilds);
            compare(dialog.currentColor,color);
            const frame=findChild(plane,"iaColorPlaneFrame") || plane.background;
            const origin=frame.mapToItem(plane,0,0);
            let ancestor=plane.parent;
            while(ancestor && !ancestor.clip) ancestor=ancestor.parent;
            verify(ancestor!==null,"Real nearest clipping ancestor exists");
            const clipOrigin=frame.mapToItem(ancestor,0,0);
            console.info("IA_PLANE_BOUNDARY "+JSON.stringify({variant:data.tag,
                frame:[origin.x,origin.y,frame.width,frame.height],plane:[plane.width,plane.height],
                clip:String(ancestor),inClip:[clipOrigin.x,clipOrigin.y,ancestor.width,ancestor.height]}));
            verify(origin.x>=0 && origin.y>=0 && origin.x+frame.width<=plane.width && origin.y+frame.height<=plane.height,
                "Complete focus frame belongs inside the plane");
            verify(clipOrigin.x>=0 && clipOrigin.y>=0 && clipOrigin.x+frame.width<=ancestor.width && clipOrigin.y+frame.height<=ancestor.height,
                "Complete focus frame survives the actual clipping ancestor");
            const edgeColor=frame.colors ? frame.colors.focusedBorderColor : plane.focusColor;
            for(const pos of [Qt.point(0.5,plane.height/2),Qt.point(plane.width-0.5,plane.height/2),
                    Qt.point(plane.width/2,0.5),Qt.point(plane.width/2,plane.height-0.5)])
                compare(TestProbe.renderedPixel(plane,pos.x,pos.y),edgeColor,"Rendered inside focus edge");
            compare(plane.contentItem.clip,true,"The plane owns endpoint-marker clipping");
        }
        function test_geometry(data): void {
            IA.Appearance.input={scheme:data.scheme,contrast:data.contrast,density:data.variant==="touch"?"touch":"compact",
                fontScale:data.variant==="font"?1.5:1,reducedMotion:true};
            scene.Window.window.contentItem.LayoutMirroring.enabled=data.variant==="rtl";
            if(data.variant==="narrow") dialog.width=340;
            open();
            const plane=item("iaColorPlane"), editor=item("iaColorEditor");
            fuzzyCompare(plane.width,plane.height,0.01); verify(plane.width<=256);
            verify(plane.width<=editor.width+0.01);
            for(const name of ["iaHueLabel","iaOpacityLabel"]) {
                const label=item(name);
                verify(label.implicitWidth<=label.width+0.1,"Full native label width: "+name);
            }
            for(const pair of [["hueSlider","iaHueValue"],["alphaSlider","iaOpacityValue"]]) {
                const slider=item(pair[0]),label=item(pair[1]);
                const sy=slider.mapToItem(editor,0,slider.height/2).y,ly=label.mapToItem(editor,0,label.height/2).y;
                fuzzyCompare(sy,ly,0.1);
                verify(label.contentWidth<=label.width+0.5);
            }
            const hueTrack=findChild(item("hueSlider"),"iaColorRailTrack");
            verify(hueTrack!==null);
            // At every appearance, actual hue artwork survives decorative borders.
            for(const fraction of [0.05,0.25,0.5,0.95]) {
                const sample=TestProbe.renderedPixel(hueTrack,hueTrack.width*fraction,hueTrack.height/2);
                verify(Math.max(sample.r,sample.g,sample.b)-Math.min(sample.r,sample.g,sample.b)>0.8,
                    "Hue artwork must remain chromatic in "+data.tag);
            }
            const box=dialog.footer as IADialogButtonBox;
            verify(box!==null); verify(box.hasRejectAction); verify(findChild(dialog,"iaDialogClose")!==null);
            const close=box.standardButton(T.DialogButtonBox.Close),caption=item("iaColorLiveCaption");
            if(data.variant!=="narrow") {
                const a=caption.mapToItem(box,0,caption.height/2).y,b=close.mapToItem(box,0,close.height/2).y;
                if(box.height<2*close.height) fuzzyCompare(a,b,0.1);
            }
            verify(caption.contentWidth<=caption.width+0.5);
            console.info("IA_COLOR_PICKER_GEOMETRY "+JSON.stringify({variant:data.tag,width:dialog.width,height:dialog.height,
                plane:[plane.width,plane.height],editor:[editor.width,editor.height],footer:[box.width,box.height]}));
        }
        function test_narrowWideRecovery(): void {
            IA.Appearance.input={fontScale:1.5,reducedMotion:true};
            dialog.width=340;
            open();
            const editor=item("iaColorEditor"), plane=item("iaColorPlane"), modeControl=item("iaColorModes");
            const natural=dialog.implicitWidth, preferred=editor.implicitWidth;
            mode(2);
            dialog.width=Qt.binding(function(){return dialog.implicitWidth;});
            verify(TestProbe.render(dialog.contentItem));
            compare(dialog.implicitWidth,natural); compare(editor.implicitWidth,preferred);
            verify(editor.width>=preferred); compare(modeControl.currentIndex,2);
            compare(item("iaColorEditor"),editor); compare(item("iaColorPlane"),plane);
            fuzzyCompare(plane.width,plane.height,0.01);
        }
        function test_liveResizeAndPixels_data(): var {
            return [{tag:"normal",contrast:"normal"},{tag:"high",contrast:"high"}];
        }
        function test_liveResizeAndPixels(data): void {
            IA.Appearance.input={scheme:"dark",contrast:data.contrast,reducedMotion:true};
            scene.applied=Qt.hsla(2/3,0.5,0.5,0.65);
            open();
            const plane=item("iaColorPlane"),surface=item("iaColorPlaneSurface"),marker=item("iaColorPlaneMarker");
            plane.controlRadius=24;
            const original=[scene.applied.r,scene.applied.g,scene.applied.b,scene.applied.a],edits=scene.edits;
            let lastWidth=plane.width,lastBuilds=TestProbe.colorPlaneRebuilds(surface);
            for(const width of [250,dialog.implicitWidth]) {
                dialog.width=width;
                tryVerify(function(){return plane.width!==lastWidth;});
                verify(TestProbe.render(plane));
                const cache=TestProbe.colorPlaneCache(surface),dpr=cache.dpr;
                compare(cache.pixelWidth,Math.ceil(plane.availableWidth*dpr));
                compare(cache.pixelHeight,Math.ceil(plane.availableHeight*dpr));
                verify(cache.rebuilds>lastBuilds);
                // A fractional scene origin can put the nominal local half-pixel
                // exactly on the clipped edge. Sample fully interior native pixels.
                const origin=plane.mapToItem(null,0,0);
                const leftPixel=(Math.ceil((origin.x+plane.leftPadding)*dpr)+0.5)/dpr-origin.x;
                const rightPixel=(Math.floor((origin.x+plane.leftPadding+plane.availableWidth)*dpr)-0.5)/dpr-origin.x;
                const black=TestProbe.renderedPixel(plane,leftPixel,plane.topPadding+plane.availableHeight/2);
                const white=TestProbe.renderedPixel(plane,rightPixel,plane.topPadding+plane.availableHeight/2);
                console.info("IA_RESIZE_EDGE_PIXELS "+JSON.stringify({dpr:dpr,origin:[origin.x,origin.y],
                    points:[leftPixel,rightPixel],black:String(black),white:String(white)}));
                compare(black,"#000000","Resized visible black endpoint");
                if(Number.isInteger(dpr)) {
                    compare(white,"#ffffff","Resized visible white endpoint");
                } else {
                    // Compare the same physical sample and geometry, not a texel
                    // endpoint that a fractional-origin window pixel interpolates.
                    const direct=directSurfaceComponent.createObject(plane.contentItem, {
                        objectName:"iaResizeDirectReference",width:plane.availableWidth,height:plane.availableHeight,hue:plane.hue,z:1000}) as IA.ColorPlaneSurface;
                    verify(direct!==null);
                    verify(TestProbe.render(plane));
                    const directOrigin=direct.mapToItem(null,0,0),surfaceOrigin=surface.mapToItem(null,0,0);
                    compare(directOrigin,surfaceOrigin);
                    compare(direct.width,surface.width);compare(direct.height,surface.height);
                    compare(TestProbe.colorPlaneCache(direct).dpr,dpr);
                    const reference=TestProbe.renderedPixel(plane,rightPixel,plane.topPadding+plane.availableHeight/2);
                    compare(white,reference,"Shaped endpoint equals direct surface at the identical physical sample");
                    console.info("IA_RESIZE_DIRECT_REFERENCE "+JSON.stringify({dpr:dpr,origin:[directOrigin.x,directOrigin.y],
                        size:[direct.width,direct.height],shaped:String(white),direct:String(reference)}));
                    direct.destroy();
                    tryVerify(()=>findChild(plane.contentItem,"iaResizeDirectReference")===null);
                    verify(TestProbe.render(plane));
                    compare(TestProbe.renderedPixel(plane,rightPixel,plane.topPadding+plane.availableHeight/2),white);
                }
                compare(TestProbe.paintedRasterPixel(surface,cache.pixelWidth-1,Math.floor(cache.pixelHeight/2),dpr),
                    "#ffffff","Actual painted intrinsic last texel remains white");
                const ix=Math.floor(cache.pixelWidth*0.2),iy=Math.floor(cache.pixelHeight*0.3);
                const expected=Qt.hsla(plane.hue,1-iy/(cache.pixelHeight-1),ix/(cache.pixelWidth-1),1);
                const pixel=TestProbe.renderedPixel(plane,plane.leftPadding+(ix+0.5)/dpr,plane.topPadding+(iy+0.5)/dpr);
                fuzzyCompare(pixel.r,expected.r,0.005);fuzzyCompare(pixel.g,expected.g,0.005);fuzzyCompare(pixel.b,expected.b,0.005);
                const center=marker.mapToItem(plane,marker.width/2,marker.height/2);
                fuzzyCompare(center.x,plane.leftPadding+plane.lightness*plane.availableWidth,0.005);
                fuzzyCompare(center.y,plane.topPadding+(1-plane.saturation)*plane.availableHeight,0.005);
                const ringOffset=marker.width/2-marker.border.width-marker.children[0].border.width/2;
                const ring=TestProbe.renderedPixel(plane,center.x+ringOffset,center.y);
                console.info("IA_RESIZE_MARKER_PIXEL "+JSON.stringify({width:marker.width,offset:ringOffset,
                    oldOffsetPixel:String(TestProbe.renderedPixel(plane,center.x+3,center.y)),ring:String(ring)}));
                verify(ring.r>0.8 && ring.g>0.8 && ring.b>0.8,"Rendered marker follows the resized data: "+String(ring));
                const corner=TestProbe.renderedPixel(plane,plane.leftPadding+0.5/dpr,plane.topPadding+0.5/dpr);
                compare(corner,TestProbe.renderedPixel(plane,0.5/dpr,0.5/dpr),"Rounded data corner retains parent exterior after resize");
                compare([scene.applied.r,scene.applied.g,scene.applied.b,scene.applied.a],original);
                compare(scene.edits,edits);
                console.info("IA_LIVE_RESIZE_PIXELS "+JSON.stringify({contrast:data.contrast,dpr:dpr,dialogWidth:width,
                    plane:[plane.width,plane.height],data:[plane.availableWidth,plane.availableHeight],cache:cache,
                    black:String(black),white:String(white),interior:[pixel.r,pixel.g,pixel.b],marker:[center.x,center.y]}));
                lastWidth=plane.width;lastBuilds=cache.rebuilds;
            }
            const x=plane.leftPadding+plane.availableWidth*0.6,y=plane.topPadding+plane.availableHeight*0.7;
            verify(TestProbe.click(plane,x,y));
            fuzzyCompare(plane.lightness,(Math.round(x)-plane.leftPadding)/plane.availableWidth,0.005);
            fuzzyCompare(plane.saturation,1-(Math.round(y)-plane.topPadding)/plane.availableHeight,0.005);
            compare(dialog.currentColor,scene.applied);
        }
        function test_dismissalOrdering_data(): var { return [{tag:"close",action:"close"},{tag:"header",action:"header"},{tag:"escape",action:"escape"}]; }
        function test_dismissalOrdering(data): void {
            open(); mode(0);
            const field=item("iaColor_hex");
            type(field,"#234567");
            const before=scene.edits;
            if(data.action==="close") click((dialog.footer as IADialogButtonBox).standardButton(T.DialogButtonBox.Close));
            else if(data.action==="header") click(item("iaDialogClose"));
            else verify(TestProbe.keyClick(field,Qt.Key_Escape));
            tryCompare(dialog,"visible",false);
            console.info("IA_COLOR_PICKER_DISMISS "+JSON.stringify({action:data.action,before:before,after:scene.edits,
                applied:String(scene.applied),draft:field.text}));
            verify(scene.edits===before || scene.edits===before+1);
            if(scene.edits>before) compare(scene.applied,"#234567");
            reopen("dismissal/"+data.action+"/after-valid");
            type(field,"#unfinished"); commit(field);
            const unchanged=Qt.rgba(scene.applied.r,scene.applied.g,scene.applied.b,scene.applied.a),count=scene.edits;
            click((dialog.footer as IADialogButtonBox).standardButton(T.DialogButtonBox.Close)); tryCompare(dialog,"visible",false);
            compare(scene.edits,count); compare(scene.applied,unchanged);
            reopen("dismissal/"+data.action+"/after-invalid"); compare(field.text,"#unfinished");
        }
    }
}
