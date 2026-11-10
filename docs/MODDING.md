# Modding guide

Mods can add **pickups** (power-ups dropped by the supply helicopter or by your own
rules), **abilities** (actions bound to a key or gamepad button), custom **projectiles**,
**balance tweaks** to the original power-ups, and can **replace any game file** (sprites,
sounds, music, level data).

Mods are written in [Lua 5.4](https://www.lua.org/manual/5.4/).

---

## Installing a mod

Put the mod's folder in a `mods` folder next to your game data (or next to the
executable, or in the working directory):

```
Heavy Weapon Deluxe/
├── data/  Fonts/  Images/  Music/  Sounds/
└── mods/
    └── MyMod/
        ├── mod.lua
        ├── Images/zap.png
        └── Sounds/zap.wav
```

- The first `mods` folder found is used (game data folder from `--data`, then
  `Heavy Weapon Deluxe/` and the working directory, then next to the executable, then the
  built-in data folder).
- Every folder in `mods/` with a `mod.lua` is loaded, in alphabetical order.
- Folders starting with `_` or `.` are skipped, which is an easy way to disable a mod.
- `--no-mods` on the command line skips the mods folder entirely.
- The title screen lists the loaded mods. Problems are written to **`mods.log`** in the
  working directory, and the title screen says so in red.

## Replacing game files

Any file in a mod folder that mirrors a game path replaces the original:
`mods/MyMod/Images/tank.png` replaces `Images/tank.png`, `mods/MyMod/Sounds/flak.ogg`
replaces `Sounds/flak.ogg`, `mods/MyMod/data/levels.xml` replaces the level list, and so on.

- Names are matched case-insensitively.
- An image may use another format than the original: `foo.png` replaces `foo.jpg`.
- PopCap images can keep their alpha in a separate mask (`foo.jpg` + `_foo.gif`). A
  replacement image only uses a mask from **its own** mod folder, so a PNG with its own
  alpha is not combined with the game's mask.
- If two mods replace the same file, the one later in alphabetical order wins.
- Keep a replaced sprite strip's frame layout (same frame count and frame size) or it
  will be cut up wrongly.

---

## Writing `mod.lua`

`mod.lua` runs once at startup. It registers content with the `hw` table; the functions
you pass are called later, during play.

```lua
hw.pickup{
    id = "medkit", image = "Images/medkit", message = "EXTRA TANK",
    on_pickup = function() hw.lives(hw.lives() + 1) end,
}

hw.ability{
    id = "zap", name = "ZAP", key = "F", button = "lefttrigger", charges = 3,
    on_use = function()
        local x, y = hw.tank()
        hw.projectile{ image = "Images/zap", x = x, y = y - 24, vy = -8, damage = 2 }
    end,
}
```

### Coordinates and time

- The playfield is 640×480. **x runs from −320 (left edge) to 320 (right edge)**, y from
  0 (top) to 480 (bottom). The tank sits at y ≈ 443; aircraft fly between y ≈ 50 and 300.
- The game runs at **100 ticks per second**. Cooldowns, timers and lifetimes are in ticks.

### Sandbox

Scripts can use `string`, `table`, `math`, `utf8` and `coroutine`, plus the `hw` API.
There is no `io`, `os`, `require`, `load` or `dofile`, so a mod cannot touch files or
the system. A single call that runs too long (about 20 million instructions, e.g. an
endless loop) is stopped with an error. `print` writes to `mods.log`.

Each mod has its own global variables; `MOD_NAME` holds the folder name.

### Errors

A script error is logged to `mods.log` with a stack trace, and the game carries on.
A mod whose `mod.lua` fails to load is skipped.

---

## API reference

Functions marked *(mission)* only work while a mission is running, i.e. inside
callbacks; elsewhere they raise an error.

### Content

#### `hw.pickup{ ... }`

A new power-up. It can drop from the supply helicopter like the originals, and from
`hw.drop`.

| Field | Default | Meaning |
| :-- | :-- | :-- |
| `id` | required | Unique name |
| `image` | `"powerups"` | Sprite: a path in the mod or game (`"Images/medkit"`, no extension needed) or a game image name |
| `frame` | 0 | Frame of the sprite strip to show |
| `cols` | image's own | Number of frames in a strip you supply |
| `message` | none | Text popping up when collected |
| `sound` | power-up sound | Sound file under `Sounds/` (`"zap.wav"`; `.ogg` is assumed without an extension) |
| `chance` | 1.0 | Drop weight relative to one original power-up (0 = only via `hw.drop`) |
| `tier` | 0 | Drops from this tier on: mission index (0-based), or survival tier |
| `max` | −1 | How many times it can be collected per run (−1 = unlimited) |
| `campaign`, `survival` | true | Which modes it drops in |
| `on_pickup` | none | `function(x, y)` called when collected |

#### `hw.ability{ ... }`

An action the player triggers. Shown on the HUD (top right) with its charges and keys.

| Field | Default | Meaning |
| :-- | :-- | :-- |
| `id` | required | Unique name |
| `name` | `id` | HUD label |
| `key` | none | Key name or list (`"F"`, `"Space"`, `"Left Shift"`: SDL key names) |
| `button` | none | Gamepad button name or list: `a`, `b`, `x`, `y`, `back`, `start`, `leftstick`, `rightstick`, `leftshoulder`, `rightshoulder`, `dpup`, `dpdown`, `dpleft`, `dpright`, `misc1`, or the triggers `lefttrigger`, `righttrigger` |
| `charges` | −1 | Charges at the start of every run (−1 = unlimited) |
| `max_charges` | −1 | Cap for `hw.give` / `hw.charges` (−1 = none) |
| `cooldown` | 0 | Ticks before it can be used again |
| `hud` | true | Show it on the HUD |
| `on_use` | required | `function()`; return `false` to not spend a charge or start the cooldown |

Abilities only fire while the tank is deployed. With no charges left or while cooling
down, the "denied" sound plays. Choose keys the game does not use (it uses A, D, W, S,
arrows, J, X, N, C, M, P, Tab, Enter, Space, Esc, Backspace, Ctrl).

#### `hw.tweak_powerup{ which = ..., ... }`

Changes one of the original power-ups. `which` is `nukes`, `shield`, `speed`, `power`,
`rapid` or `spread`.

| Field | Meaning |
| :-- | :-- |
| `max` | Level cap (originals: nukes 3, shield 3, spread 4, others unlimited) |
| `unlock` | How fast it unlocks: offered while level ≤ tier × unlock (0 = always) |
| `chance` | Drop weight (1 = original; 0 = never drops) |
| `message` | Pickup text |

Raising `spread` above 4 is not supported (the cannon sprite has four rows).

#### `hw.on(event, function)`

| Event | Arguments | When |
| :-- | :-- | :-- |
| `level_start` | `mission, survival` | A mission or survival run begins |
| `tick` | | Every tick during a mission |
| `enemy_killed` | `enemy` | An aircraft, vehicle or boss part is destroyed (see `hw.enemies` for the table) |
| `tank_destroyed` | `lives` | The tank was destroyed (`lives` = tanks left in reserve) |
| `pickup` | `id, x, y` | A mod pickup was collected |

#### `hw.after(ticks, function)` *(timers)*

Calls the function after `ticks` ticks. Pending timers are dropped when the mission ends.

### Player and game state *(mission)*

| Function | Returns / does |
| :-- | :-- |
| `hw.tank()` | `x, y, deployed` |
| `hw.lives([n])` | Tanks in reserve; sets it when `n` is given |
| `hw.score([points, x, y])` | Score; adds `points` (with a popup at x, y) when given |
| `hw.upgrade(slot [, level])` | Level of an upgrade slot; sets it when `level` is given. Slots: `nukes`, `shield`, `speed`, `power`, `rapid`, `spread`, `orbs`, `homing`, `laser`, `rockets`, `flak`, `thunderstrike` (armory weapons go up to 3) |
| `hw.mission()` | `mission, survival, progress (0..1), tier` |
| `hw.invulnerable(ticks)` | The tank cannot be destroyed for that long |
| `hw.megalaser(ticks)` | Megalaser mode for that long (0 ends it) |
| `hw.charges(id [, n])` | Charges of an ability; sets them when `n` is given (works outside missions too) |
| `hw.give(id [, n])` | Adds `n` (default 1) charges, up to `max_charges` |

### Weapons and effects *(mission)*

| Function | Does |
| :-- | :-- |
| `hw.fire(kind, ...)` | Fires a built-in weapon: `"shell"` (power, side offset, angle offset), `"homing"`, `"rocket"` (side ±1), `"thunderstrike"`, `"nuke"` (uses a nuke, like the nuke button) |
| `hw.projectile{ ... }` | Launches a custom projectile (below) |
| `hw.explosion(x, y [, size [, sound]])` | Explosion effect (size default 80; `sound = false` for silence). Does no damage by itself |
| `hw.damage_area(x, y, radius, amount [, friendly])` | Damages every enemy within the radius; returns how many were hit |
| `hw.drop(pickup, x, y [, vx, vy])` | Drops a mod pickup by id, or an original by slot name (`"shield"`, `"homing"`...) |
| `hw.message(text [, x, y [, red]])` | Small popup text (default: above the tank) |
| `hw.big_message(text)` | Large centre-screen text like "GET READY" |
| `hw.sound(sound [, x [, volume]])` | Plays a sound: a file name under `Sounds/`, or an original sound id from `hw.SND` (`powerup`, `gunpowerup`, `shieldup`, `speedup`, `upgrade`, `denied`, `bigexplode`, `smallexplode`, `laser`, `missile`, `nukeblast`, `thunder`, `alert`, `star`, `tankfire`). `x` pans original sound ids |

#### `hw.projectile{ ... }`

| Field | Default | Meaning |
| :-- | :-- | :-- |
| `image` | required | Sprite path or game image name |
| `cols`, `frame`, `anim` | image's own, 0, 0 | Strip frame count, start frame, frames advanced per tick |
| `x`, `y` | above the tank | Start position |
| `vx`, `vy` | 0, −6 | Velocity in pixels per tick |
| `gravity` | 0 | Added to `vy` every tick |
| `homing` | 0 | Max turn per tick (radians) toward the nearest enemy, keeping its speed |
| `damage` | 1 | Damage per hit (compare with the enemies' `max_hp` from `hw.enemies()`) |
| `pierce` | false | Keep flying after a hit (hits each enemy once) |
| `life` | 0 | Ticks before it disappears (0 = until it leaves the screen) |
| `glow` | false | Draw additively (bright, glowing) |
| `flip` | false | Mirror the sprite while moving left |
| `on_hit` | none | `function(enemy, x, y)` called on each hit, before the damage |

### Enemies *(mission)*

`hw.enemies()` returns a list of tables with `x`, `y`, `vx`, `vy`, `hp`, `max_hp`, `type`
(craft id from `craft.xml`), `w`, `h`, `boss`, `friendly`, `hittable`. Friendly units
(your allied helicopters) are included; check `friendly`.

| Function | Does |
| :-- | :-- |
| `hw.damage(enemy, amount)` | Damages that enemy (with hit flash, sparks, or a kill); returns false if it is already gone |

The tables are snapshots: read them right away rather than keeping them across ticks.

### Other

| | |
| :-- | :-- |
| `hw.log(...)` / `print(...)` | Writes to `mods.log` |
| `hw.API_VERSION` | `"1"`; will change if the API changes incompatibly |
| `MOD_NAME` | This mod's folder name |
