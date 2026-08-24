// Корпус Token Monitor для Waveshare ESP32-S3-Touch-LCD-4 Rev4.0
//
// Три детали: передняя рамка, задняя крышка и подставка.
// Скрепляется 4 винтами M3 (саморезы по пластику, M3x12).
// Дисплей снимается с подставки — «ласточкин хвост».
//
// Экспорт STL из командной строки:
//   openscad -o front.stl -D "part=\"front\"" token_monitor_case.scad
//   openscad -o back.stl  -D "part=\"back\""  token_monitor_case.scad
//   openscad -o stand.stl -D "part=\"stand\"" token_monitor_case.scad
//
// В графическом OpenSCAD: измените part ниже, F6, затем File > Export > STL.

/* [Что показывать] */
// front | back | stand | assembly (для просмотра в сборе)
part = "assembly";

/* [Размеры платы — из чертежа Waveshare] */
glass_w      = 84.2;   // стекло с тачем, ширина и высота
glass_t      = 2.2;    // толщина стекла (проверить штангенциркулем)
pcb_w        = 74.5;
pcb_h        = 76.5;
pcb_t        = 1.6;
hole_dx      = 66.5;   // шаг монтажных отверстий платы
hole_dy      = 68.5;
active_w     = 71.86;  // активная область экрана
active_h     = 70.18;

/* [Зазоры и стенки] */
clr          = 0.4;    // зазор вокруг стекла
wall         = 2.4;    // толщина стенок
face_t       = 2.0;    // толщина передней грани (рамки)
back_t       = 2.4;    // толщина задней крышки
window_pad   = 1.5;    // окно больше активной области с каждой стороны,
                       // чтобы неточность посадки не срезала пиксели
corner_r     = 4;      // скругление внешних углов

/* [Глубина] */
// Замер по месту: от лицевой поверхности стекла до самой выступающей детали
// сзади (винтовой клеммник). Это единственный размер, который задаёт толщину.
stack_h      = 15.0;
z_clr        = 0.8;    // воздушный зазор между клеммником и крышкой
module_gap   = 3.0;    // от задней поверхности стекла до лица платы

/* [Крепёж] */
screw_pilot  = 2.8;    // отверстие под саморез M3 в передней рамке
screw_free   = 3.3;    // проходное отверстие в задней крышке
screw_head   = 6.2;    // потай под головку
post_r       = 2.75;   // радиус стоек в углах
post_xy      = 39.8;   // стойки стоят за стеклом, вплотную к стенкам

/* [USB-C — положение уточнить линейкой] */
usb_y        = -11.7;  // смещение центра разъёма от центра платы (вниз)
usb_slot_w   = 13.0;   // ширина проёма (вдоль края)
usb_slot_h   = 6.5;    // высота проёма

/* [Подставка] */
tilt         = 15;     // угол наклона экрана, градусы
base_w       = 92;
base_d       = 78;
base_t       = 6;
rail_w       = 24;     // «ласточкин хвост»: широкая часть (снаружи)
rail_top     = 17;     //                    узкая часть (у крышки)
rail_h       = 34;     // длина направляющей
rail_d       = 5;      // выступ над крышкой
rail_y       = -6;     // смещение рельса вниз от центра крышки
rail_slack   = 0.35;   // зазор посадки рельса в паз

$fn = 48;

/* ---------- производные величины ---------- */
inner       = glass_w + 2 * clr;              // внутренний размер под стекло
outer       = glass_w + 2 * (clr + wall);      // внешний габарит корпуса, 90 мм
depth_in    = stack_h - glass_t + z_clr;       // от стекла до задней крышки
back_clear  = depth_in - module_gap - pcb_t;   // высота втулок, прижимающих плату
pcb_front_z = face_t + glass_t + module_gap;   // лицо платы от передней грани
pcb_back_z  = pcb_front_z + pcb_t;
body_h      = face_t + glass_t + depth_in;     // высота передней части

echo(str("Толщина корпуса в сборе: ", body_h + back_t, " мм"));
echo(str("Высота втулок под плату: ", back_clear, " мм"));
assert(back_clear > 2, "stack_h слишком мал: плата не помещается");

module rrect(w, h, r, height) {
    linear_extrude(height)
        offset(r = r) offset(r = -r) square([w, h], center = true);
}

/* ---------- передняя рамка ---------- */

module screw_posts(h, hole_d, hole_h) {
    for (x = [-1, 1], y = [-1, 1])
        translate([x * post_xy, y * post_xy, 0])
            difference() {
                cylinder(r = post_r, h = h);
                translate([0, 0, h - hole_h])
                    cylinder(d = hole_d, h = hole_h + 0.1);
            }
}

module front_shell() {
    difference() {
        union() {
            // корпус
            rrect(outer, outer, corner_r, body_h);
        }

        // окно под экран
        translate([0, 0, -0.1])
            rrect(active_w + 2 * window_pad, active_h + 2 * window_pad,
                  2, face_t + 0.2);

        // карман под стекло
        translate([0, 0, face_t])
            rrect(inner, inner, 2, glass_t + 0.1);

        // внутренняя полость под плату и компоненты
        translate([0, 0, face_t + glass_t])
            rrect(outer - 2 * wall, outer - 2 * wall, 2, depth_in + 0.1);

        // проём под USB-C в левом торце
        translate([-outer / 2 - 1, usb_y, pcb_back_z - 1.2])
            cube([wall + 4, usb_slot_w, usb_slot_h], center = false);
    }

    // стойки под винты — начинаются за стеклом, поэтому корпус остаётся узким
    translate([0, 0, face_t + glass_t])
        screw_posts(depth_in, screw_pilot, depth_in - 1);
}

/* ---------- задняя крышка ---------- */

module vent_slots() {
    // косые прорези, как на референсе
    for (i = [0 : 6])
        translate([-24 + i * 8, 26, -0.1])
            rrect(2.6, 16, 1.2, back_t + 0.2);
}

module pcb_pins() {
    // втулки, прижимающие плату к дисплейному модулю,
    // с центрирующим штырьком в монтажные отверстия
    for (x = [-1, 1], y = [-1, 1])
        translate([x * hole_dx / 2, y * hole_dy / 2, back_t]) {
            cylinder(d = 6, h = back_clear);
            translate([0, 0, back_clear]) cylinder(d = 2.8, h = 2.2);
        }
}

// «Ласточкин хвост»: узкая часть у поверхности (z=0), широкая снаружи (z=-rail_d),
// поэтому паз держит рельс и не даёт ему выйти наружу.
module rail_shape(len, slack = 0) {
    hull() {
        translate([0, 0, -0.005])
            cube([rail_top + 2 * slack, len, 0.01], center = true);
        translate([0, 0, -rail_d])
            cube([rail_w + 2 * slack, len, 0.01], center = true);
    }
}

module back_shell() {
    difference() {
        union() {
            rrect(outer, outer, corner_r, back_t);
            pcb_pins();
            // рельс на наружной стороне крышки (внутрь смотрят стойки под плату)
            translate([0, rail_y, 0]) rail_shape(rail_h);
        }

        // отверстия под винты с потаем
        for (x = [-1, 1], y = [-1, 1])
            translate([x * post_xy, y * post_xy, -0.1]) {
                cylinder(d = screw_free, h = back_t + 0.2);
                cylinder(d = screw_head, h = 1.6);
            }

        vent_slots();
    }
}

/* ---------- подставка ---------- */

pillar_w   = 44;   // ширина наклонной опоры
pillar_h   = 66;   // высота опоры вдоль лицевой плоскости
pillar_t   = 13;   // толщина опоры
face_y     = base_d / 2 - 30;   // опора смещена вперёд: дисплей отклоняется
                                // назад, а центр тяжести остаётся над основанием
// Паз начинается так, чтобы нижняя грань дисплея не упиралась в основание.
slot_start = 26;   // ниже паза остаётся упор, на который садится рельс

// Система координат лицевой (наклонной) плоскости опоры:
// локальные +Y — вверх вдоль плоскости, +Z — наружу, к дисплею.
module on_face() {
    translate([0, face_y, base_t]) rotate([90 - tilt, 0, 0]) children();
}

module stand() {
    difference() {
        union() {
            // основание с фаской
            hull() {
                translate([0, 0, base_t - 0.1]) rrect(base_w - 8, base_d - 8, 8, 0.1);
                rrect(base_w, base_d, 8, 0.1);
            }
            // наклонная опора
            on_face()
                translate([0, pillar_h / 2, -pillar_t / 2])
                    minkowski() {
                        cube([pillar_w - 6, pillar_h - 6, pillar_t - 3], center = true);
                        sphere(r = 3);
                    }
        }

        // паз под рельс, открытый сверху — дисплей надевается сдвигом вниз
        on_face()
            translate([0, slot_start + (pillar_h + 20) / 2, 0])
                rail_shape(pillar_h + 20, rail_slack);

        // всё, что ушло ниже стола, срезаем
        translate([0, 0, -50]) cube([300, 300, 100], center = true);

        // облегчение снизу и место для кабеля
        translate([0, 0, -0.1]) rrect(base_w - 20, base_d - 20, 8, 2.6);
    }
}

/* ---------- сборка для просмотра ---------- */

// Дисплей в сборе: рамка + стекло + плата + крышка.
// Начало координат — центр лицевой грани, экран смотрит в −Z.
module display_unit() {
    color("WhiteSmoke") front_shell();
    color("Gainsboro")
        translate([0, 0, body_h + back_t]) rotate([180, 0, 0]) back_shell();
    color("Black", 0.65)
        translate([0, 0, face_t]) rrect(glass_w, glass_w, 2, glass_t);
    color("DarkGreen", 0.75)
        translate([0, 0, pcb_front_z]) rrect(pcb_w, pcb_h, 1, pcb_t);
}

// Дисплей, надетый на подставку: рельс крышки входит в паз опоры,
// экран смотрит от опоры наружу.
module assembly() {
    color("Silver") stand();
    on_face()
        translate([0, slot_start + rail_h / 2 - rail_y, body_h + back_t])
            rotate([0, 180, 0])
                display_unit();
}

if (part == "front")        front_shell();
else if (part == "back")    back_shell();
else if (part == "stand")   stand();
else                        assembly();
