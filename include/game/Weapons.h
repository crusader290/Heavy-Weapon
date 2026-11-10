#pragma once

// The tank's armory weapons (see src/game/Weapons.cpp).

namespace HeavyWeapon {

class Board;

// One flak volley (0x44a220): bursts at random points around the aim point.
struct FlakBurst {
    FlakBurst(Board& board, double cx, double cy);
    void Update();
    void Draw();

    Board& b;
    double centerX, centerY;
    int px, py;
    double frame = 0.0;
    int count = 0, bursts, radius;
    bool dead = false;
};

void FireHomingMissile(Board& b);
void FireRocket(Board& b, double gunCel, int side);
void FireStatic(Board& b);

} // namespace HeavyWeapon
