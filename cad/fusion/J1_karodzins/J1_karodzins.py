"""Fusion 360 skripts: J1 karodziņš optiskajam gala slēdzim.

Uzbūvē jaunu komponenti "J1 karodzins": rumba centrā + plecs līdz sensoram + mēlīte uz leju,
kas iebrauc sensora spraugā no augšas (sprauga atveras uz augšu).

Palaišana: UTILITIES -> ADD-INS -> Scripts and Add-Ins -> zaļais "+" pie "My Scripts"
           -> izvēlies šo mapi -> J1_karodzins -> Run.
Izmērus maini zemāk un palaid vēlreiz (vecā komponente paliek - izdzēs to).

Visi izmēri mm. Augstums z = 0 ir NEKUSTĪGĀ vāka augšējā virsma.
Drukā MELNĀ krāsā: balta plastmasa laiž cauri infrasarkano gaismu.
"""
import math
import traceback

import adsk.core
import adsk.fusion

# ---- IZMĒRI: nomēri ar bīdmēru un ieraksti ----------------------------------------
HUB_D = 40.0          # rumbas diametrs (ieiet alumīnija gredzenā / uz izejas atloka)
RECESS = 5.0          # cik dziļi izejas atloka virsma ir zem vāka virsmas
SCREW_D = 5.5         # centrālās skrūves caurums (M5 -> 5.5)
SCREW_HEAD_D = 9.5    # skrūves galvas iegremdējums no augšas (0 = nav)
SCREW_HEAD_H = 4.0
PIN_HOLES = []        # papildu caurumi: (attālums no centra, leņķis °, diametrs), piem. [(12, 90, 3.2)]

CLEAR = 4.5           # plecs paceļas tik augstu virs vāka (skrūvju galvas ~3 mm + rezerve)
SENSOR_TOP = 0.0      # sensora augšmala virs vāka virsmas (plecam jāiet pāri)
ARM_H = 4.0           # pleca biezums
ARM_W = 10.0          # pleca platums

R = 62.0              # no ass centra līdz spraugas vidum
SLOT_W = 3.0          # sensora spraugas platums
SLOT_GAP = 0.6        # brīvums katrā pusē: karodziņa biezums = SLOT_W - 2 * SLOT_GAP
FLAG_W = 5.0          # karodziņa platums pa loku (šajā leņķī J1 nevar braukt)
AXIS_DEPTH = 6.0      # sensora caurumiņš (optiskā ass) zem vāka virsmas
OVER = 2.0            # cik karodziņš iet zem optiskās ass (nedrīkst aizsniegt spraugas dibenu)
# ------------------------------------------------------------------------------------


def mm(v):
    return v / 10.0   # Fusion API garumi ir cm


def pt(x, y):
    return adsk.core.Point3D.create(mm(x), mm(y), 0)


def sketch_at(comp, z):
    if abs(z) < 1e-9:
        return comp.sketches.add(comp.xYConstructionPlane)
    planes = comp.constructionPlanes
    pi = planes.createInput()
    pi.setByOffset(comp.xYConstructionPlane, adsk.core.ValueInput.createByReal(mm(z)))
    pl = planes.add(pi)
    pl.isLightBulbOn = False
    return comp.sketches.add(pl)


def circle(comp, z, cx, cy, d):
    sk = sketch_at(comp, z)
    sk.sketchCurves.sketchCircles.addByCenterRadius(pt(cx, cy), mm(d / 2))
    return sk.profiles.item(0)


def rect(comp, z, x0, y0, x1, y1):
    sk = sketch_at(comp, z)
    sk.sketchCurves.sketchLines.addTwoPointRectangle(pt(x0, y0), pt(x1, y1))
    return sk.profiles.item(0)


def extrude(comp, prof, h, op):
    ex = comp.features.extrudeFeatures
    inp = ex.createInput(prof, op)
    inp.setDistanceExtent(False, adsk.core.ValueInput.createByReal(mm(h)))
    return ex.add(inp)


def run(context):
    ui = None
    try:
        app = adsk.core.Application.get()
        ui = app.userInterface
        design = adsk.fusion.Design.cast(app.activeProduct)
        if not design:
            ui.messageBox("Atver dizainu (DESIGN darba vide) un palaid vēlreiz.")
            return

        flag_t = SLOT_W - 2 * SLOT_GAP
        problems = []
        if flag_t < 1.0:
            problems.append(f"karodziņš tikai {flag_t:.2f} mm biezs - samazini SLOT_GAP")
        if CLEAR < SENSOR_TOP + 1.0:
            problems.append("plecs skars sensoru - palielini CLEAR")
        if R - flag_t / 2 < HUB_D / 2:
            problems.append("R mazāks par rumbu")
        if problems:
            ui.messageBox("Pārbaudi izmērus:\n- " + "\n- ".join(problems))
            return

        new = adsk.fusion.FeatureOperations.NewBodyFeatureOperation
        join = adsk.fusion.FeatureOperations.JoinFeatureOperation
        cut = adsk.fusion.FeatureOperations.CutFeatureOperation

        occ = design.rootComponent.occurrences.addNewComponent(adsk.core.Matrix3D.create())
        comp = occ.component
        comp.name = "J1 karodzins"
        top = CLEAR + ARM_H

        # rumba: no izejas atloka līdz pleca augšai
        body = extrude(comp, circle(comp, -RECESS, 0, 0, HUB_D), RECESS + top, new).bodies.item(0)
        body.name = "karodzins"
        # plecs līdz sensoram (+X virzienā)
        extrude(comp, rect(comp, CLEAR, 0, -ARM_W / 2, R + flag_t / 2, ARM_W / 2), ARM_H, join)
        # mēlīte uz leju spraugā (0.5 mm pārklājas ar plecu, lai savienojas)
        bottom = -(AXIS_DEPTH + OVER)
        extrude(comp, rect(comp, bottom, R - flag_t / 2, -FLAG_W / 2, R + flag_t / 2, FLAG_W / 2),
                CLEAR - bottom + 0.5, join)
        # caurumi
        extrude(comp, circle(comp, -RECESS, 0, 0, SCREW_D), RECESS + top, cut)
        if SCREW_HEAD_D > 0:
            extrude(comp, circle(comp, top - SCREW_HEAD_H, 0, 0, SCREW_HEAD_D), SCREW_HEAD_H, cut)
        for r, ang, d in PIN_HOLES:
            a = math.radians(ang)
            extrude(comp, circle(comp, -RECESS, r * math.cos(a), r * math.sin(a), d), RECESS + top, cut)

        dead = math.degrees((FLAG_W + flag_t) / R)
        ui.messageBox(f"Gatavs: karodziņš {flag_t:.2f} × {FLAG_W:.1f} mm, rādiuss {R:.1f} mm.\n"
                      f"Karodziņš aizņem ~{dead:.1f}° - tik daudz J1 kustībai paliek mazāk par 360°.\n"
                      f"Drukā melnā krāsā.")
    except Exception:
        if ui:
            ui.messageBox("Kļūda:\n" + traceback.format_exc())
