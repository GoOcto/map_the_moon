import numpy as np
import pyvista as pv

print("Generating 6000x6000 mesh (36 million points)...")

# 1. Create the grid coordinates
x = np.linspace(-10, 10, 6000)
y = np.linspace(-10, 10, 6000)
xx, yy = np.meshgrid(x, y)

# 2. Create the Z values (the surface)
# (Replace this with your actual Z data)
zz = np.sin(np.sqrt(xx**2 + yy**2))

# 3. Create the PyVista StructuredGrid object
# This is the most efficient way for a 6000x6000 grid
mesh = pv.StructuredGrid(xx, yy, zz)

# Optional: Add scalar data to color the mesh
mesh["elevation"] = zz.ravel(order="F")

print("Rendering...")

# 4. Plot the mesh
# This will open a fast, native OpenGL window
mesh.plot(show_edges=False, cmap='viridis')