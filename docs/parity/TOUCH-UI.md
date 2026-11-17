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

[observed] Desktop, GLES and iOS-host builds complete with 24/29/29
warnings. All nine requested regression targets pass. Two invariant shots
with the rear mirror disabled and fresh user directories match their
references byte for byte. Reviewed prompt and active overlays before and
after at 1170x643 and 2048x1536: ENTER clears the car and the point-sized
controls no longer grow with frame height. These host captures have a 1x
logical/drawable ratio; they do not establish physical-device behavior.

[observed] The full 4201-frame S7 career-start regression exits successfully
and passes all 11 rules in s7_carlot.spec. Line-buffered stdout/stderr are
required for reliable combined-log assertions: an initial run split the
purchase message with stderr output and failed two rules. That failed run
is excluded from passes.
