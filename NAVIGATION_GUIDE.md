# Improved Mouse Navigation Guide

## The Problem
The original mouse navigation was frustrating because:
- Cursor was locked (couldn't see where you were clicking)
- Always in FPS mode (not intuitive for viewing terrain)
- No way to rotate around a point of interest

## The Solution

The viewer now has **TWO camera modes** that you can switch between:

### 🔵 Orbit Mode (Default) - MUCH BETTER for terrain viewing!

This is like Blender, Maya, or most 3D modeling software.

**Mouse Controls:**
- **Left-click + drag** → Rotate camera around the terrain
- **Right-click + drag** → Pan the view (move target point)
- **Scroll wheel** → Zoom in/out smoothly

**Keyboard:**
- **WASD** → Move the target point you're orbiting around
- **Q/E** → Move target up/down
- **Shift** → Move faster

**Why it's better:**
✓ Cursor is visible (not locked)
✓ Intuitive rotation around terrain
✓ Easy to focus on specific features
✓ Smooth zoom in/out
✓ Like CAD/3D modeling software

### 🎮 FPS Mode - For flying around

Press **Space** to switch to FPS mode if you want game-like free movement.

**Mouse:**
- **Move mouse** → Look around (cursor is hidden and locked)

**Keyboard:**
- **WASD** → Move forward/back/left/right
- **Q/E** → Move up/down
- **Shift** → Sprint (faster movement)

**Why use this:**
- Good for flying through the terrain
- Navigate like a video game
- Explore from ground level

## Quick Reference

```
┌─────────────────────────────────────────────────┐
│  ORBIT MODE (default) - Best for viewing!      │
├─────────────────────────────────────────────────┤
│  Left drag    → Rotate around terrain          │
│  Right drag   → Pan camera                     │
│  Scroll       → Zoom in/out                    │
│  WASD/QE      → Move target point              │
└─────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────┐
│  Press SPACE to toggle FPS mode                │
└─────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────┐
│  Other Keys                                     │
├─────────────────────────────────────────────────┤
│  R            → Reset camera to default view    │
│  Tab          → Toggle wireframe                │
│  Shift        → Move faster                     │
│  ESC          → Quit                            │
└─────────────────────────────────────────────────┘
```

## Tips

1. **Start in Orbit Mode** (default) - it's much more intuitive
2. **Left-click and drag** to rotate - you can see the cursor now!
3. **Scroll to zoom** - smooth and natural
4. **Right-click to pan** if you need to move the view
5. **Press Space** only if you want to fly around FPS-style
6. **Press R** anytime to reset if you get lost

## What Changed

Before:
- ❌ Cursor always locked/hidden
- ❌ Only FPS mode (confusing for terrain viewing)
- ❌ Hard to rotate around points of interest
- ❌ Zoom changed FOV (weird feeling)

After:
- ✅ Cursor visible in orbit mode
- ✅ Orbit mode by default (like Blender/Maya)
- ✅ Rotate around terrain naturally
- ✅ Zoom moves camera closer/farther (natural)
- ✅ Easy mode switching with Space
- ✅ Reset with R key

Try it now: `./lunar_viewer`

The controls should feel much more natural!
