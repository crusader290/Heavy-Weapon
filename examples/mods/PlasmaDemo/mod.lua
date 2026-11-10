-- PlasmaDemo: an example mod showing every part of the API (see docs/MODDING.md).
-- Copy this folder to `mods/PlasmaDemo/` next to your game data to try it.
--
--   * two new pickups: a medkit (+1 tank) and a plasma cell (+3 plasma charges)
--   * two abilities: plasma bolts (F / left trigger) and an airstrike (G / right stick)
--   * a balance tweak to an original power-up
--   * event hooks: plasma cells sometimes drop from destroyed aircraft

-- ---- pickups --------------------------------------------------------------------------

hw.pickup{
    id = "medkit",
    image = "Images/medkit",      -- this mod's Images/medkit.png
    message = "EXTRA TANK",
    sound = "zap.wav",            -- this mod's Sounds/zap.wav
    chance = 0.4,                 -- 40% as likely as one original power-up
    tier = 2,                     -- not before mission 3 (survival: tier 2)
    max = 2,                      -- at most twice per run
    on_pickup = function(x, y)
        hw.lives(hw.lives() + 1)
    end,
}

hw.pickup{
    id = "plasma_cell",
    image = "Images/plasmacell",
    message = "PLASMA +3",
    sound = "zap.wav",
    chance = 1.0,
    on_pickup = function(x, y)
        hw.give("plasma", 3)
    end,
}

-- ---- abilities ----------------------------------------------------------------------------

hw.ability{
    id = "plasma",
    name = "PLASMA",
    key = "F",
    button = "lefttrigger",
    charges = 3,                  -- start of every run; -1 would be unlimited
    max_charges = 9,
    cooldown = 40,                -- ticks (100 per second)
    on_use = function()
        local x, y = hw.tank()
        for i = -2, 2 do
            hw.projectile{
                image = "Images/plasma", cols = 4, anim = 0.25,
                x = x, y = y - 24,
                vx = i * 1.2, vy = -7,
                homing = 0.06,        -- radians per tick toward the nearest enemy
                damage = 3,
                life = 300,
                glow = true,
                on_hit = function(enemy, hx, hy)
                    hw.explosion(hx, hy, 40, false)
                end,
            }
        end
        hw.sound("zap.wav")
    end,
}

hw.ability{
    id = "airstrike",
    name = "AIRSTRIKE",
    key = "G",
    button = "rightstick",
    charges = 1,
    max_charges = 2,
    cooldown = 500,
    on_use = function()
        local _, survival = hw.mission()
        hw.big_message("AIRSTRIKE")
        hw.sound(hw.SND.alert)
        for i = 0, 9 do
            hw.after(60 + i * 10, function()
                local x = -290 + i * 64
                hw.explosion(x, 140, 110)
                hw.damage_area(x, 140, 90, 6)
            end)
        end
    end,
}

-- ---- tweaks ---------------------------------------------------------------------------------

-- Shields drop half again as often.
hw.tweak_powerup{ which = "shield", chance = 1.5 }

-- ---- events ---------------------------------------------------------------------------------

-- 1 in 25 destroyed aircraft leaves a plasma cell behind (never bosses).
hw.on("enemy_killed", function(enemy)
    if not enemy.boss and not enemy.friendly and math.random(25) == 1 then
        hw.drop("plasma_cell", enemy.x, enemy.y, enemy.vx * 0.5, 0)
    end
end)

hw.on("level_start", function(mission, survival)
    hw.log("mission " .. (mission + 1) .. (survival and " (survival)" or "") ..
           ", plasma charges " .. hw.charges("plasma"))
end)
