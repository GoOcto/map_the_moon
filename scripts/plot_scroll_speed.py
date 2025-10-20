import numpy as np
import matplotlib.pyplot as plt

# Constants from the C++ code
kMinCameraDistance = 1750.0
kMaxCameraDistance = 20000.0
kScrollMinSpeed = 60.0
kScrollMaxSpeed = 1800.0

def compute_scroll_zoom_speed(camera_distance):
    """
    Python implementation of the C++ computeScrollZoomSpeed function.
    """
    range_dist = max(kMaxCameraDistance - kMinCameraDistance, 1.0)
    ratio = (camera_distance - kMinCameraDistance) / range_dist
    ratio = np.clip(ratio, 0.0, 1.0)
    eased = ratio * ratio * ratio  # smooth ease-out
    return kScrollMinSpeed + (kScrollMaxSpeed - kScrollMinSpeed) * eased

# Generate a range of camera distances
camera_distances = np.linspace(kMinCameraDistance, kMaxCameraDistance, 500)

# Calculate the corresponding scroll speeds
scroll_speeds = compute_scroll_zoom_speed(camera_distances)

# Create the plot
plt.figure(figsize=(10, 6))
plt.plot(camera_distances, scroll_speeds)
plt.title('Scroll Zoom Speed vs. Camera Distance')
plt.xlabel('Camera Distance')
plt.ylabel('Scroll Zoom Speed')
plt.grid(True)
plt.tight_layout()

# Show the plot
plt.show()
