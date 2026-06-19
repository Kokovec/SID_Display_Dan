import cv2
import numpy as np
from PIL import Image
import os

folder = r"C:\vscode\SID_Play_Images"
bmp_path = os.path.join(folder, "controls.bmp")

img = cv2.imread(bmp_path)
if img is None:
    pil_img = Image.open(bmp_path).convert("RGB")
    pil_img.save(os.path.join(folder, "controls_fixed.png"))
    img = cv2.imread(os.path.join(folder, "controls_fixed.png"))

h, w = img.shape[:2]
print(f"✅ Loaded: {w}x{h}")

# --- Grayscale + blur ---
gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
gray_blur = cv2.medianBlur(gray, 5)

# --- Hough circle detection ---
circles = cv2.HoughCircles(
    gray_blur,
    cv2.HOUGH_GRADIENT,
    dp=1.2,
    minDist=40,        # distance between centers
    param1=100,        # Canny high threshold
    param2=20,         # accumulator threshold (lower = more circles)
    minRadius=10,
    maxRadius=40
)

if circles is None:
    print("❌ No circles detected by HoughCircles")
    cv2.imwrite(os.path.join(folder, "debug_gray.png"), gray)
    cv2.imwrite(os.path.join(folder, "debug_gray_blur.png"), gray_blur)
    raise SystemExit

circles = np.round(circles[0, :]).astype("int")
print(f"Found {len(circles)} raw circles")

# Convert to list of (x, y, r)
raw_circles = [(int(x), int(y), int(r)) for (x, y, r) in circles]

# --- Split into two horizontal rows (middle vs bottom) ---
ys = np.array([c[1] for c in raw_circles])
median_y = np.median(ys)

middle_row = [c for c in raw_circles if c[1] < median_y]
bottom_row = [c for c in raw_circles if c[1] >= median_y]

middle_row.sort(key=lambda c: c[0])
bottom_row.sort(key=lambda c: c[0])

middle_row = middle_row[:4]
bottom_row = bottom_row[:4]

final_circles = middle_row + bottom_row
print(f"🎯 Final circles: {len(final_circles)}")

centers = [(x, y) for x, y, r in final_circles]

print("\n🎯 DETECTED CENTERS:")
for i, (x, y) in enumerate(centers, 1):
    print(f"   {i}. ({x:3d}, {y:3d})")

# Save centers
txt_path = os.path.join(folder, "circle_centers.txt")
with open(txt_path, "w") as f:
    for x, y in centers:
        f.write(f"{x},{y}\n")

# Annotate
annotated = img.copy()
for x, y in centers:
    cv2.circle(annotated, (x, y), 26, (0, 255, 0), 3)
    cv2.circle(annotated, (x, y), 5, (0, 0, 255), -1)

cv2.imwrite(os.path.join(folder, "detected_controls.png"), annotated)
print(f"\n✅ Saved {len(centers)} centers to circle_centers.txt")
