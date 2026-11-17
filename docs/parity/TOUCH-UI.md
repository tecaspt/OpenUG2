# Touch controls

[inferred] SDL window coordinates are logical points on iOS. Layout uses
window height to convert point offsets and radii into height-normalized
coordinates; drawing then scales these by drawable height. Thus a backing
scale of two produces twice the pixels for the same point-sized target.
Mouse normalization continues to use logical window coordinates. Finger
coordinates use normalized coordinates and share the rendered circles.

[inferred] Steering and pedals have 56-point diameters. Other controls have
44-point diameters. Insets and spacing are also in points. Pause and camera
sit at the upper left. ENTER sits at the right, 220 points above the bottom,
above the driving controls and away from the car. Shop entry remains visible
before the first touch. Menus suppress the overlay.

[observed] touch-controls-test passes 406 checks, zero failures, including
point diameters at logical heights 321, 643, 768 and 1536, entry placement,
non-overlapping targets, input edges, suppression and shop discoverability.
Physical-device validation is not yet performed.
