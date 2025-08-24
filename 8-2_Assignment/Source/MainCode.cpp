// 8-2 Assignment – Enhanced “Action Arena”
// - Spiral/arc brick layout with mixed styles & sizes
// - Paddle control (A/D or Left/Right)
// - Physics-based circles: velocity vectors, wall reflections, friction on bottom
// - Brick states: multi-hit destructible bricks with color change
// - Circle-circle elastic collisions with color blending
//
// Build notes: Same dependencies as your original (GLFW, OpenGL). Windows-friendly includes kept.
// This uses immediate-mode OpenGL for clarity in an assignments context.

#include <GLFW/glfw3.h>
#include "linmath.h" // (not strictly required here, but left in to match your setup)
#include <stdlib.h>
#include <stdio.h>
#include <conio.h>
#include <iostream>
#include <vector>
#include <windows.h>
#include <time.h>
#include <cmath>
#include <algorithm>

using namespace std;

// ------------------------------- Utilities & Constants -------------------------------

const float DEG2RAD = 3.1415926535f / 180.0f;

// World bounds in NDC (-1..1). We leave a small epsilon so objects don’t stick.
const float LEFT_BOUND = -1.0f;
const float RIGHT_BOUND = 1.0f;
const float TOP_BOUND = 1.0f;
const float BOTTOM_BOUND = -1.0f;

void processInput(GLFWwindow* window);

// ------------------------------- Types -------------------------------

enum BRICKTYPE { REFLECTIVE, DESTRUCTABLE };
enum ONOFF { ON, OFF };

// Simple clamp helper
static float clampf(float v, float lo, float hi) { return max(lo, min(v, hi)); }

// -------------------------------- Brick --------------------------------

class Brick
{
public:
    // Center position
    float x, y;
    // Half-size extents (hx, hy) to make AABB math precise and easy
    float hx, hy;

    // Visual
    float red, green, blue;

    // Behavior/state
    BRICKTYPE brick_type;
    ONOFF onoff;

    // For destructible bricks
    int max_hits;
    int hits_taken;

    Brick(BRICKTYPE bt, float cx, float cy, float full_width,
        float r, float g, float b, float full_height = 0.0f, int hp = 1)
    {
        brick_type = bt;
        x = cx; y = cy;
        float w = (full_height <= 0.0f) ? full_width : full_width;
        float h = (full_height <= 0.0f) ? full_width : full_height;
        hx = w * 0.5f;
        hy = h * 0.5f;

        red = r; green = g; blue = b;
        onoff = ON;

        max_hits = max(1, hp);
        hits_taken = 0;
    }

    void drawBrick() const
    {
        if (onoff == OFF) return;

        // Slight color darkening by damage for destructibles
        float cr = red, cg = green, cb = blue;
        if (brick_type == DESTRUCTABLE && max_hits > 1)
        {
            float dmg = (float)hits_taken / (float)max_hits; // 0..1
            cr = clampf(red * (1.0f - 0.5f * dmg) + 0.2f * dmg, 0.0f, 1.0f);
            cg = clampf(green * (1.0f - 0.5f * dmg) + 0.2f * dmg, 0.0f, 1.0f);
            cb = clampf(blue * (1.0f - 0.5f * dmg) + 0.2f * dmg, 0.0f, 1.0f);
        }

        glColor3f(cr, cg, cb);
        glBegin(GL_POLYGON);
        glVertex2f(x + hx, y + hy);
        glVertex2f(x + hx, y - hy);
        glVertex2f(x - hx, y - hy);
        glVertex2f(x - hx, y + hy);
        glEnd();
    }

    // AABB-circle overlap test (circle center vs AABB)
    bool overlapsCircle(float cx, float cy, float radius) const
    {
        if (onoff == OFF) return false;
        // Clamp circle center to AABB to find closest point
        float closestX = clampf(cx, x - hx, x + hx);
        float closestY = clampf(cy, y - hy, y + hy);
        float dx = cx - closestX;
        float dy = cy - closestY;
        return (dx * dx + dy * dy) <= (radius * radius);
    }

    // On hit, update state. Return true if brick should reflect the ball.
    bool onHit()
    {
        if (brick_type == REFLECTIVE) {
            return true;
        }
        hits_taken++;
        if (hits_taken >= max_hits) {
            onoff = OFF;
        }
        return true;
    }
};

// -------------------------------- Circle --------------------------------

class Circle
{
public:
    float x, y;          // position
    float vx, vy;        // velocity
    float radius;
    float red, green, blue;

    float speed_scale = 1.0f;

    Circle(float cx, float cy, float rad, float init_vx, float init_vy,
        float r, float g, float b)
    {
        x = cx; y = cy; radius = rad;
        vx = init_vx; vy = init_vy;
        red = r; green = g; blue = b;
    }

    void draw() const
    {
        glColor3f(red, green, blue);
        glBegin(GL_POLYGON);
        for (int i = 0; i < 64; i++) {
            float a = (i / 64.0f) * 360.0f * DEG2RAD;
            glVertex2f(x + cosf(a) * radius, y + sinf(a) * radius);
        }
        glEnd();
    }

    void integrate(float dt)
    {
        x += vx * dt * speed_scale;
        y += vy * dt * speed_scale;
    }

    void handleWorldBounds()
    {
        if (x - radius <= LEFT_BOUND) {
            x = LEFT_BOUND + radius;
            vx = -vx;
            speed_scale *= 1.05f;
        }
        else if (x + radius >= RIGHT_BOUND) {
            x = RIGHT_BOUND - radius;
            vx = -vx;
            speed_scale *= 1.05f;
        }

        if (y - radius <= BOTTOM_BOUND) {
            y = BOTTOM_BOUND + radius;
            vy = -vy;
            speed_scale *= 0.90f;
        }
        else if (y + radius >= TOP_BOUND) {
            y = TOP_BOUND - radius;
            vy = -vy;
            speed_scale *= 1.02f;
        }

        speed_scale = clampf(speed_scale, 0.25f, 2.5f);
    }
};

// ------------------------------- Globals -------------------------------

vector<Circle> world;
vector<Brick>  bricks;
Brick paddle(REFLECTIVE, 0.0f, -0.85f, 0.35f,
    0.95f, 0.95f, 0.95f, 0.06f, 1);

static double g_lastTime = 0.0;

// --------------------------- Forward Decls -----------------------------

void spawnInitialLayout();
void spawnCircle();
void handleCollisions();
void handleCircleCircleCollisions();
void movePaddle(GLFWwindow* window, float dt);

// ------------------------------- Main ----------------------------------

int main(void)
{
    srand((unsigned int)time(NULL));

    if (!glfwInit()) {
        exit(EXIT_FAILURE);
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(800, 800,
        "8-2 Assignment – Action Arena", NULL, NULL);
    if (!window) {
        glfwTerminate();
        exit(EXIT_FAILURE);
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    spawnInitialLayout();

    g_lastTime = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);
        glClear(GL_COLOR_BUFFER_BIT);

        double now = glfwGetTime();
        float dt = (float)(now - g_lastTime);
        g_lastTime = now;
        dt = clampf(dt, 0.0f, 0.0333f);

        processInput(window);
        movePaddle(window, dt);

        for (auto& c : world) {
            c.integrate(1.0f);
            c.handleWorldBounds();
        }

        handleCollisions();
        handleCircleCircleCollisions();

        for (const auto& b : bricks) b.drawBrick();
        paddle.drawBrick();
        for (const auto& c : world) c.draw();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    exit(EXIT_SUCCESS);
}

// --------------------------- World Building ----------------------------

void spawnInitialLayout()
{
    bricks.clear();

    // Spiral/arc parameters
    const int   COUNT = 34;
    const float R_START = 0.25f;
    const float R_END = 0.85f;
    const float THETA_START = 30.0f * DEG2RAD;
    const float THETA_END = 300.0f * DEG2RAD;

    for (int i = 0; i < COUNT; ++i)
    {
        float t = (float)i / (float)(COUNT - 1);
        float r = R_START + t * (R_END - R_START);
        float th = THETA_START + t * (THETA_END - THETA_START);

        float cx = r * cosf(th);
        float cy = r * sinf(th) * 0.8f;

        bool big = (i % 3 == 0);
        float full_w = big ? 0.18f : 0.12f;
        float full_h = big ? 0.10f : 0.10f;

        BRICKTYPE type = (i % 2 == 0) ? DESTRUCTABLE : REFLECTIVE;

        float hue = t;
        float rcol = 0.5f + 0.5f * cosf(6.28318f * hue);
        float gcol = 0.5f + 0.5f * cosf(6.28318f * (hue + 0.33f));
        float bcol = 0.5f + 0.5f * cosf(6.28318f * (hue + 0.66f));

        int hp = (type == DESTRUCTABLE) ? (big ? 4 : 3) : 1;

        bricks.emplace_back(type, cx, cy, full_w, rcol, gcol, bcol, full_h, hp);
    }

    spawnCircle();
}

void spawnCircle()
{
    float rad = 0.045f;

    float ang = (float)(rand()) / (float)RAND_MAX * 2.0f * 3.1415926535f;
    float spd = 0.008f + ((float)(rand()) / (float)RAND_MAX) * 0.012f;
    float vx = cosf(ang) * spd;
    float vy = sinf(ang) * spd;

    float r = 0.4f + (rand() % 600) / 1000.0f;
    float g = 0.4f + (rand() % 600) / 1000.0f;
    float b = 0.4f + (rand() % 600) / 1000.0f;

    world.emplace_back(0.0f, 0.0f, rad, vx, vy, r, g, b);
}

// ------------------------------ Input ----------------------------------

void processInput(GLFWwindow* window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
    {
        if (world.size() < 12) {
            spawnCircle();
        }
    }
}

void movePaddle(GLFWwindow* window, float dt)
{
    if (paddle.onoff == OFF) return;

    float speed = 1.6f * dt;

    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) {
        paddle.x -= speed;
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
        paddle.x += speed;
    }

    float minX = LEFT_BOUND + paddle.hx;
    float maxX = RIGHT_BOUND - paddle.hx;
    paddle.x = clampf(paddle.x, minX, maxX);
}

// ------------------------------ Collisions -----------------------------

static void reflect(float& vx, float& vy, float nx, float ny)
{
    float dot = vx * nx + vy * ny;
    vx = vx - 2.0f * dot * nx;
    vy = vy - 2.0f * dot * ny;
}

void handleCircleBrickCollision(Circle& c, Brick& b)
{
    if (b.onoff == OFF) return;
    if (!b.overlapsCircle(c.x, c.y, c.radius)) return;

    float dx = c.x - b.x;
    float dy = c.y - b.y;

    float overlapX = (b.hx + c.radius) - fabsf(dx);
    float overlapY = (b.hy + c.radius) - fabsf(dy);

    if (overlapX < overlapY) {
        float nx = (dx >= 0.0f) ? 1.0f : -1.0f;
        float ny = 0.0f;
        c.x += nx * overlapX;
        reflect(c.vx, c.vy, nx, ny);
        c.speed_scale *= 1.02f;
    }
    else {
        float nx = 0.0f;
        float ny = (dy >= 0.0f) ? 1.0f : -1.0f;
        c.y += ny * overlapY;
        reflect(c.vx, c.vy, nx, ny);
        c.speed_scale *= 1.02f;
    }

    b.onHit();

    c.red = clampf(c.red * 0.95f + b.red * 0.15f, 0.0f, 1.0f);
    c.green = clampf(c.green * 0.95f + b.green * 0.15f, 0.0f, 1.0f);
    c.blue = clampf(c.blue * 0.95f + b.blue * 0.15f, 0.0f, 1.0f);
}

void handleCollisions()
{
    for (auto& c : world) {
        handleCircleBrickCollision(c, paddle);
    }

    for (auto& c : world) {
        for (auto& b : bricks) {
            handleCircleBrickCollision(c, b);
        }
    }
}

void handleCircleCircleCollisions()
{
    const float restitution = 1.0f;

    for (size_t i = 0; i < world.size(); ++i) {
        for (size_t j = i + 1; j < world.size(); ++j) {
            Circle& a = world[i];
            Circle& b = world[j];

            float dx = b.x - a.x;
            float dy = b.y - a.y;
            float dist2 = dx * dx + dy * dy;
            float rsum = a.radius + b.radius;

            if (dist2 <= rsum * rsum) {
                float dist = sqrtf(max(dist2, 1e-6f));
                float nx = dx / dist;
                float ny = dy / dist;

                float penetration = rsum - dist + 0.0005f;
                a.x -= nx * (penetration * 0.5f);
                a.y -= ny * (penetration * 0.5f);
                b.x += nx * (penetration * 0.5f);
                b.y += ny * (penetration * 0.5f);

                float rvx = b.vx - a.vx;
                float rvy = b.vy - a.vy;
                float velAlongNormal = rvx * nx + rvy * ny;

                if (velAlongNormal > 0.0f) continue;

                float jimpulse = -(1.0f + restitution) * velAlongNormal / 2.0f;
                float jx = jimpulse * nx;
                float jy = jimpulse * ny;

                a.vx -= jx;
                a.vy -= jy;
                b.vx += jx;
                b.vy += jy;

                float mix = 0.35f;
                float ar = a.red, ag = a.green, ab = a.blue;
                float br = b.red, bg = b.green, bb = b.blue;
                a.red = clampf((1 - mix) * ar + mix * br, 0.0f, 1.0f);
                a.green = clampf((1 - mix) * ag + mix * bg, 0.0f, 1.0f);
                a.blue = clampf((1 - mix) * ab + mix * bb, 0.0f, 1.0f);

                b.red = clampf((1 - mix) * br + mix * ar, 0.0f, 1.0f);
                b.green = clampf((1 - mix) * bg + mix * ag, 0.0f, 1.0f);
                b.blue = clampf((1 - mix) * bb + mix * ab, 0.0f, 1.0f);
            }
        }
    }
}
