// SPDX-License-Identifier: LGPL-3.0-or-later
#include "ColorEditState.h"
#include <QSignalSpy>
#include <QTest>
using InterActive::ColorEditState;
class ColorEditStateTest : public QObject
{
    Q_OBJECT
private slots:
    void precisionAndFeedback()
    {
        ColorEditState state;
        const QColor input = QColor::fromRgba64(12345, 23456, 34567, 45678);
        QSignalSpy edits(&state, &ColorEditState::colorEdited);
        state.synchronize(input);
        for (int mode = 0; mode < 4; ++mode) state.setMode(mode);
        QCOMPARE(state.currentColor(), input);
        QCOMPARE(edits.size(), 0);
        state.setDraft("rgbRed", "57");
        connect(&state, &ColorEditState::colorEdited, &state, [&state](const QColor &value) {
            QCOMPARE(state.currentColor(), value);
            state.synchronize(value.toRgb());
        });
        QVERIFY(state.commit("rgbRed"));
        QCOMPARE(edits.size(), 1);
        QCOMPARE(state.currentColor().greenF(), input.greenF());
        QCOMPARE(state.currentColor().blueF(), input.blueF());
        QCOMPARE(state.currentColor().alphaF(), input.alphaF());
        QVERIFY(state.commit("rgbRed"));
        QCOMPARE(edits.size(), 1);
        state.editOpacity(0.12345);
        QCOMPARE(edits.size(), 2);
        state.editOpacity(0.12345);
        QCOMPARE(edits.size(), 2);
    }
    void domainsAndDrafts()
    {
        ColorEditState state;
        connect(&state, &ColorEditState::colorEdited, &state, &ColorEditState::synchronize);
        QSignalSpy edits(&state, &ColorEditState::colorEdited);
        for (const auto &field : {"rgbRed", "hsvSaturation", "hslLightness", "hexAlpha"}) {
            state.setDraft(field, "-1"); QVERIFY(!state.commit(field));
            QVERIFY(!state.error(field).isEmpty()); QCOMPARE(state.text(field), "-1");
        }
        QCOMPARE(edits.size(), 0);
        state.synchronize(QColor("#40808080"));
        QCOMPARE(state.text("rgbRed"), "-1");
        state.setMode(3); state.setMode(0);
        QCOMPARE(state.text("rgbRed"), "-1");
        for (const auto &text : {"#123", "#123456789", "garbage"}) {
            state.setDraft("hex", text); QVERIFY(!state.commit("hex"));
        }
        state.setDraft("hex", "#80123456"); QVERIFY(state.commit("hex"));
        QCOMPARE(state.currentColor(), QColor("#80123456"));
        state.setDraft("hex", "#123456"); QVERIFY(state.commit("hex"));
        QCOMPARE(state.currentColor().alpha(), 255);
        state.setDraft("rgbBlue", "1.5"); QVERIFY(!state.commit("rgbBlue"));
        state.setDraft("hslLightness", "33.333"); QVERIFY(state.commit("hslLightness"));
        QVERIFY(qAbs(state.lightness() - 0.33333) < 0.000001);
    }
    void hueMemoryAndEndpoints()
    {
        ColorEditState state;
        connect(&state, &ColorEditState::colorEdited, &state, &ColorEditState::synchronize);
        state.synchronize(QColor::fromHslF(0.75, 0.8, 0.4, 0.6));
        state.editPlane(0.8, 0); QCOMPARE(state.hue(), 270.0); QCOMPARE(state.saturation(), 0.8);
        state.synchronize(state.currentColor().toRgb()); QCOMPARE(state.saturation(), 0.8);
        state.editHue(360); QCOMPARE(state.hue(), 360.0);
        state.editOpacity(0.25); QCOMPARE(state.hue(), 360.0); QCOMPARE(state.saturation(), 0.8);
        state.editPlane(0.8, 0.5); QVERIFY(state.currentColor().redF() > state.currentColor().blueF());
        state.synchronize(QColor("#808080")); QCOMPARE(state.hue(), 360.0);
        state.synchronize(QColor::fromHsvF(0.5, 0, 0.3)); QCOMPARE(state.hue(), 180.0);
        state.setMode(2);
        state.setDraft("hsvSaturation", "70.5"); QVERIFY(state.commit("hsvSaturation"));
        state.setDraft("hsvValue", "0"); QVERIFY(state.commit("hsvValue"));
        state.editHue(240);
        QCOMPARE(state.hue(), 240.0);
        state.setDraft("hsvValue", "50"); QVERIFY(state.commit("hsvValue"));
        QVERIFY(qAbs(state.currentColor().hsvSaturationF() - 0.705) < 0.0001);
        QVERIFY(state.currentColor().blueF() > state.currentColor().redF());
    }
    void controlledAuthority()
    {
        ColorEditState state;
        state.synchronize(QColor("#246789"));
        QSignalSpy edits(&state, &ColorEditState::colorEdited);
        state.setDraft("hex", "#123456");
        QVERIFY(state.commit("hex"));
        QCOMPARE(edits.size(), 1);
        QCOMPARE(state.currentColor(), QColor("#246789"));
        QCOMPARE(state.text("hex"), "#123456");
        QVERIFY(state.pending("hex"));
        auto transformed = connect(&state, &ColorEditState::colorEdited, &state,
            [&state](const QColor &) { state.synchronize(QColor("#654321")); });
        QVERIFY(state.commit("hex"));
        QCOMPARE(edits.size(), 2);
        QCOMPARE(state.currentColor(), QColor("#654321"));
        QVERIFY(state.pending("hex"));
        disconnect(transformed);
        connect(&state, &ColorEditState::colorEdited, &state, &ColorEditState::synchronize);
        QVERIFY(state.commit("hex"));
        QCOMPARE(edits.size(), 3);
        QCOMPARE(state.currentColor(), QColor("#123456"));
        QVERIFY(!state.pending("hex"));
        QVERIFY(state.commit("hex"));
        QCOMPARE(edits.size(), 3);
    }
    void hsvHueAtBlackWithRgbAcknowledgement()
    {
        ColorEditState state;
        state.setMode(2);
        connect(&state, &ColorEditState::colorEdited, &state, [&state](const QColor &value) {
            state.synchronize(value.toRgb());
        });
        state.synchronize(QColor::fromHsvF(.5, .7, .3, .6));
        state.setDraft("hsvValue", "0"); QVERIFY(state.commit("hsvValue"));
        state.editHue(240);
        state.editOpacity(.25);
        state.setDraft("hsvValue", "50"); QVERIFY(state.commit("hsvValue"));
        QVERIFY(qAbs(state.currentColor().hsvSaturationF() - .7) < .0001);
        QVERIFY(qAbs(state.currentColor().hsvHueF() - 2.0 / 3) < .0001);
        QVERIFY(qAbs(state.currentColor().alphaF() - .25) < .0001);
    }
    void rgbAcknowledgementKeepsNativeCoordinates()
    {
        ColorEditState state;
        QColor applied;
        connect(&state, &ColorEditState::colorEdited, &state, [&state,&applied](const QColor &value) {
            applied=value.toRgb();
            state.synchronize(applied);
        });
        state.synchronize(QColor("#668f3699"));
        state.editHue(299);
        const double saturation=0.8031433820724487, lightness=0.8031433820724487;
        state.editPlane(saturation,lightness);
        QCOMPARE(state.currentColor().redF(),applied.redF());
        QCOMPARE(state.currentColor().greenF(),applied.greenF());
        QCOMPARE(state.currentColor().blueF(),applied.blueF());
        QCOMPARE(state.currentColor().alphaF(),applied.alphaF());
        QCOMPARE(state.saturation(),saturation);
        QCOMPARE(state.lightness(),lightness);
        const auto channels=state.currentColor().toRgb().rgba64();
        const auto expected=applied.rgba64();
        QCOMPARE(channels.red(),expected.red()); QCOMPARE(channels.green(),expected.green());
        QCOMPARE(channels.blue(),expected.blue()); QCOMPARE(channels.alpha(),expected.alpha());
    }
};
QTEST_GUILESS_MAIN(ColorEditStateTest)
#include "tst_color_edit_state.moc"
