// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QtTest
import InterActive.Foundation as IA
import InterActive.Tests
import "iconconsumer" as Consumer

Rectangle {
    id: scene
    width: 640
    height: 200
    color: "white"
    Component { id: consumerComponent; Consumer.IconConsumer {} }
    Component { id: surfaceComponent; IA.ColorPlaneSurface { width: 64; height: 64; hue: 0.67 } }
    Component { id: themeComponent; IA.ThemeIconItem {
        width: 64; height: 64
        name: Qt.resolvedUrl("iconconsumer/icons/consumer-mark.svg").toString().replace("file://", "")
        color: "#c04020"
    } }
    Loader { id: rendererLoader; active: false }
    Window { id: migrationWindow; width: 160; height: 160; visible: false; color: "white" }
    TestCase {
        name: "IconSources"
        when: windowShown

        function test_rendererDestruction_data(): var {
            return [{tag:"plane-destroy",plane:true,loader:false},{tag:"icon-destroy",plane:false,loader:false},
                    {tag:"plane-loader",plane:true,loader:true},{tag:"icon-loader",plane:false,loader:true}];
        }
        function test_rendererDestruction(data): void {
            const component=data.plane?surfaceComponent:themeComponent;
            if(data.loader) {
                rendererLoader.sourceComponent=component;
                rendererLoader.active=true;
                tryCompare(rendererLoader,"status",Loader.Ready);
                verify(TestProbe.render(rendererLoader.item));
                rendererLoader.active=false;
                compare(rendererLoader.item,null);
            } else {
                const renderer=component.createObject(scene);
                verify(renderer!==null);
                verify(TestProbe.render(renderer));
                renderer.destroy();
                wait(0); // Deliver deferred deletion while the host window stays alive.
            }
            verify(TestProbe.render(scene));
            console.info("IA_RENDERER_DESTROYED "+data.tag);
        }
        function test_rendererMigration_data(): var {
            return [{tag:"plane",plane:true},{tag:"icon",plane:false}];
        }
        function test_rendererMigration(data): void {
            const renderer=(data.plane?surfaceComponent:themeComponent).createObject(null);
            verify(renderer!==null);
            function checkRaster(dpr) {
                if(data.plane) {
                    const cache=TestProbe.colorPlaneCache(renderer);
                    compare(cache.dpr,dpr);
                    compare(cache.pixelWidth,Math.ceil(renderer.width*dpr));
                    compare(TestProbe.paintedRasterPixel(renderer,cache.pixelWidth-1,Math.floor(cache.pixelHeight/2),dpr),"#ffffff");
                } else {
                    compare(renderer.available,true);
                    compare(TestProbe.paintedRasterPixel(renderer,Math.floor(32*dpr),Math.floor(32*dpr),dpr),"#c04020");
                }
            }
            checkRaster(1);
            renderer.parent=scene;
            verify(TestProbe.render(renderer));
            checkRaster(TestProbe.effectiveDpr(renderer));
            renderer.parent=null;
            checkRaster(1);
            migrationWindow.visible=true;
            renderer.parent=migrationWindow.contentItem;
            tryVerify(()=>TestProbe.exposed(renderer));
            verify(TestProbe.render(renderer));
            const dpr=TestProbe.effectiveDpr(renderer);
            checkRaster(dpr);
            renderer.parent=scene;
            verify(TestProbe.render(renderer));
            checkRaster(dpr);
            if(!data.plane) {
                renderer.color="#2040c0";
                verify(TestProbe.render(renderer));
                compare(TestProbe.paintedRasterPixel(renderer,Math.floor(32*dpr),Math.floor(32*dpr),dpr),"#2040c0");
                renderer.name="__ia_missing_lifetime_icon__";
                compare(renderer.available,false);
                compare(TestProbe.paintedRasterPixel(renderer,0,0,dpr).a,0);
            }
            renderer.destroy();wait(0);
            migrationWindow.visible=false;
            verify(TestProbe.render(scene));
        }

        function iconIn(item: Item): IA.Icon {
            for (const child of item.children) {
                const icon = child as IA.Icon;
                if (icon !== null && icon.visible)
                    return icon;
                const nested = iconIn(child);
                if (nested !== null)
                    return nested;
            }
            return null;
        }

        function test_relativeConsumerSource_data(): var {
            const rows = [];
            const types = ["button", "tool", "tab", "delegate", "menuItem"];
            for (let i = 0; i < types.length; ++i) {
                for (const mode of ["original", "missingName", "tinted"]) {
                    rows.push({tag: types[i] + "-" + mode, index: i, mode: mode});
                }
            }
            return rows;
        }

        function test_relativeConsumerSource(data: var): void {
            const fixture = createTemporaryObject(consumerComponent, scene, {
                requestedName: data.mode === "missingName" ? "__ia_nonexistent_fixture_icon__" : "",
                tint: data.mode === "tinted" ? "#80c04020" : "transparent"
            }) as Consumer.IconConsumer;
            verify(fixture !== null);
            const control = fixture.controls[data.index] as T.AbstractButton;
            verify(control !== null);
            const icon = iconIn(control.contentItem);
            verify(icon !== null);
            let sourceImage = null;
            for (const child of icon.children) {
                if (child instanceof Image)
                    sourceImage = child as Image;
            }
            verify(sourceImage !== null);
            tryCompare(sourceImage, "status", Image.Ready, 1000);
            compare(sourceImage.source, Qt.resolvedUrl("iconconsumer/icons/consumer-mark.svg"));
            compare(control.icon.source.toString(), "icons/consumer-mark.svg", "Do not mutate the caller's icon input");
            verify(TestProbe.render(icon));
            // Qt 6.8 grabImage crops by local x/y, so grab the root and map the sample into it.
            const pixels = grabImage(scene);
            verify(pixels.width > 0 && pixels.height > 0);
            const point = icon.mapToItem(scene, icon.width / 2, icon.height / 2);
            const frame = data.index === 2 ? findChild(control.background, "iaSegmentFrame") as Rectangle
                : control.background as Rectangle;
            verify(frame !== null);
            const background = frame.color;
            const alpha = 128 / 255;
            const expected = data.mode === "tinted"
                ? Qt.rgba(192 / 255 * alpha + (background.r * background.a + 1 - background.a) * (1 - alpha),
                          64 / 255 * alpha + (background.g * background.a + 1 - background.a) * (1 - alpha),
                          32 / 255 * alpha + (background.b * background.a + 1 - background.a) * (1 - alpha), 1)
                : Qt.rgba(32 / 255, 160 / 255, 96 / 255, 1);
            fuzzyCompare(pixels.pixel(Math.floor(point.x * pixels.width / scene.width),
                                      Math.floor(point.y * pixels.height / scene.height)),
                         expected, 2, "Consumer-relative icon pixels, including tint alpha composited over the surface");
        }
    }
}
