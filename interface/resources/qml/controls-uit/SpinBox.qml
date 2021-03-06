//
//  SpinBox.qml
//
//  Created by David Rowe on 26 Feb 2016
//  Copyright 2016 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

import QtQuick 2.7
import QtQuick.Controls 2.2

import "../styles-uit"
import "../controls-uit" as HifiControls

SpinBox {
    id: spinBox

    property int colorScheme: hifi.colorSchemes.light
    readonly property bool isLightColorScheme: colorScheme === hifi.colorSchemes.light
    property string label: ""
    property string labelInside: ""
    property color colorLabelInside: hifi.colors.white
    property real controlHeight: height + (spinBoxLabel.visible ? spinBoxLabel.height + spinBoxLabel.anchors.bottomMargin : 0)
    property int decimals: 2;
    property real factor: Math.pow(10, decimals)

    property real minimumValue: 0.0
    property real maximumValue: 0.0

    property real realValue: 0.0
    property real realFrom: 0.0
    property real realTo: 100.0
    property real realStepSize: 1.0

    implicitHeight: height
    implicitWidth: width

    padding: 0
    leftPadding: 0
    rightPadding: padding + (up.indicator ? up.indicator.width : 0)
    topPadding: 0
    bottomPadding: 0

    locale: Qt.locale("en_US")

    onValueModified: realValue = value/factor
    onValueChanged: realValue = value/factor

    stepSize: realStepSize*factor
    value: realValue*factor
    to : realTo*factor
    from : realFrom*factor

    font.family: "Fira Sans SemiBold"
    font.pixelSize: hifi.fontSizes.textFieldInput
    height: hifi.fontSizes.textFieldInput + 13  // Match height of TextField control.

    y: spinBoxLabel.visible ? spinBoxLabel.height + spinBoxLabel.anchors.bottomMargin : 0

    background: Rectangle {
        color: isLightColorScheme
               ? (spinBox.activeFocus ? hifi.colors.white : hifi.colors.lightGray)
               : (spinBox.activeFocus ? hifi.colors.black : hifi.colors.baseGrayShadow)
        border.color: spinBoxLabelInside.visible ? spinBoxLabelInside.color : hifi.colors.primaryHighlight
        border.width: spinBox.activeFocus ? spinBoxLabelInside.visible ? 2 : 1 : 0
    }

    validator: DoubleValidator {
        bottom: Math.min(spinBox.from, spinBox.to)*spinBox.factor
        top:  Math.max(spinBox.from, spinBox.to)*spinBox.factor
    }

    textFromValue: function(value, locale) {
        return parseFloat(value*1.0/factor).toFixed(decimals);
    }

    valueFromText: function(text, locale) {
        return Number.fromLocaleString(locale, text);
    }


    contentItem: TextInput {
        z: 2
        color: isLightColorScheme
               ? (spinBox.activeFocus ? hifi.colors.black : hifi.colors.lightGray)
               : (spinBox.activeFocus ? hifi.colors.white : hifi.colors.lightGrayText)
        selectedTextColor: hifi.colors.black
        selectionColor: hifi.colors.primaryHighlight
        text: spinBox.textFromValue(spinBox.value, spinBox.locale)
        verticalAlignment: Qt.AlignVCenter
        leftPadding: spinBoxLabelInside.visible ? 30 : hifi.dimensions.textPadding
        //rightPadding: hifi.dimensions.spinnerSize
        width: spinBox.width - hifi.dimensions.spinnerSize
    }
    up.indicator: Item {
        x: spinBox.width - implicitWidth - 5
        y: 1
        clip: true
        implicitHeight: spinBox.implicitHeight/2
        implicitWidth: spinBox.implicitHeight/2
        HiFiGlyphs {
            anchors.centerIn: parent
            text: hifi.glyphs.caratUp
            size: hifi.dimensions.spinnerSize
            color: spinBox.down.pressed || spinBox.up.hovered ? (isLightColorScheme ? hifi.colors.black : hifi.colors.white) : hifi.colors.gray
        }
    }

    down.indicator: Item {
            x: spinBox.width - implicitWidth - 5
            y: spinBox.implicitHeight/2
            clip: true
            implicitHeight: spinBox.implicitHeight/2
            implicitWidth: spinBox.implicitHeight/2
            HiFiGlyphs {
                anchors.centerIn: parent
                text: hifi.glyphs.caratDn
                size: hifi.dimensions.spinnerSize
                color: spinBox.down.pressed || spinBox.down.hovered ? (isLightColorScheme ? hifi.colors.black : hifi.colors.white) : hifi.colors.gray
            }
    }

    HifiControls.Label {
        id: spinBoxLabel
        text: spinBox.label
        colorScheme: spinBox.colorScheme
        anchors.left: parent.left
        anchors.bottom: parent.top
        anchors.bottomMargin: 4
        visible: label != ""
    }

    HifiControls.Label {
        id: spinBoxLabelInside
        text: spinBox.labelInside
        anchors.left: parent.left
        anchors.leftMargin: 10
        font.bold: true
        anchors.verticalCenter: parent.verticalCenter
        color: spinBox.colorLabelInside
        visible: spinBox.labelInside != ""
    }

//    MouseArea {
//        anchors.fill: parent
//        propagateComposedEvents: true
//        onWheel: {
//            if(spinBox.activeFocus)
//                wheel.accepted = false
//            else
//                wheel.accepted = true
//        }
//        onPressed: {
//            mouse.accepted = false
//        }
//        onReleased: {
//            mouse.accepted = false
//        }
//        onClicked: {
//            mouse.accepted = false
//        }
//        onDoubleClicked: {
//            mouse.accepted = false
//        }
//    }
}
