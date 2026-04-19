# 🎯 Block Movement Recording System

---

## 🧭 How to Use

### Step 1: Enter Edit Mode
- Press **`E`** to enter Edit Mode  
- HUD will display: **“EDIT MODE”**  
- All blocks will show a **dim yellow highlight**

---

### Step 2: Select a Block for Recording
- Hold **`Alt`**
- **Left-click** on the block you want to record
- Selected block will show an **orange highlight**
- Recording starts **immediately**

---

### Step 3: Record Movement
- Keep **Alt held down**
- Drag the mouse to move the block
- Block:
  - Follows cursor
  - Snaps to grid
- Movement keyframes are captured automatically  
- Debug console will show keyframe logs

---

### Step 4: Stop Recording
- Release **mouse button** (or Alt)

A confirmation popup will appear showing:
- Block serial number  
- Total duration  
- Number of keyframes  
- Visual path of movement  

---

### Step 5: Confirm or Cancel

#### Visualization Includes:
- Grid background  
- Cyan path line  
- Keyframe dots (green → red gradient)  
- Y-level annotations  
- START / END labels  
- Coordinate range  

#### Confirm
- Movement is saved  
- Duration is locked  
- Block marked as recorded  

#### Cancel
- Movement discarded  
- Block resets  

---

### Step 6: Playback Movement
- Press **`E`** to exit Edit Mode  
- Click **Play**

Block will:
- Play audio at `startTimeSec`  
- Move through recorded positions  
- Update at each keyframe  

---

## ⚠️ Important Notes

### Duration Locking
- Duration becomes **locked after recording**
- Cannot edit in Block Edit popup
- Ensures timing consistency

---

### Start Time Adjustment
- You **can change start time**
- Entire movement shifts accordingly
- Relative timing remains intact

---

### Movement Constraints
- Must be valid grid position  
- Cannot overlap other blocks  
- Cannot move to origin `(0,0,0)`  
- Must stay within bounds **(-40 to 40)**  

---

# 🏗 Architecture & Implementation

---

## Component Overview
