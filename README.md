# ColorFinder

ColorFinder is a Windows utility for artists who want to identify and retexture
parts of a VTube Studio Live2D model. It temporarily turns every connected
texture part into a unique color, so you can click a visible part of the model
and find the texture file that contains it.

## Important: make a backup first

Before using ColorFinder, make a complete copy of the VTube Studio model folder.

`imageColorer.exe` changes the texture images in place and removes blend and
color effects from the model's `.moc3` file. These changes are intentional, but
they are not automatically reversible. Work on the copy, not your original
model.

## Model requirements

The selected model root should contain:

- A `.moc3` file.
- A texture folder whose name ends with a numeric suffix, for example
  `MyModel.2048`.
- Texture images inside that folder, such as `.png`, `.jpg`, `.jpeg`, `.bmp`,
	`.gif`, `.tif`, `.tiff`, or `.webp` files.

## Workflow

### 1. Select the model root

Run `setup.exe` and select the root folder of your copied model. Do not select
the texture folder itself. The selected path is stored in the
`COLORFINDER_IMAGE_DIR` user environment variable so the other programs know
which model to use.

### 2. Prepare the textures

Run `imageColorer.exe`.

It will:

1. Find the child texture folder ending in `.number`.
2. Detect each texture by connected non-transparent pixels.
3. Give every texture part a different, strongly separated RGB color across all
	texture atlases.
4. Make visible texture pixels fully opaque while keeping the background
	transparent.
5. Replace the original texture files in the copied model.
6. Remove blend modes and color effects from the model's `.moc3` file.
7. Create `colorfinder_colors.json` inside the numbered texture folder so the
	picker can find the correct image quickly.

Do not run this step on your original model. Run it again only when the model
or its textures change, because it recolors files in place and regenerates the
color map.

### 3. Find a texture part

Run `picker.exe`.

The mouse cursor changes to a crosshair. The picker window tells you what to do
and displays the last RGB color selected. Click any visible part of the model
on screen, including a model shown in VTube Studio.

The picker uses the color map to identify the most likely texture image instead
of scanning every image. It accepts a small RGB difference to account for
display scaling, color management, and photo-editor changes.

When a match is found, the matching image opens in an OpenCV window:

- Matching pixels are bright red.
- Other visible texture pixels are grey.
- Transparent areas use a checkerboard background.
- The original texture file is not changed by the picker.

If no indexed color is close enough, the picker reports `No matching Texture
found`. Close all matching image windows to exit after a successful match.

Press `Esc` to exit at any time. The normal mouse cursor is restored only when the
program closes correctly. If the program crashes unexpectedly or is closed using the X-button the cursor cannot be restord.
To restore the normal cursor you need to reopen the program and close it correctly.

## Portable package

The `ColorFinder_dist` folder contains the Windows executables and required
OpenCV/runtime DLLs. Keep the DLLs beside the executables when sharing or
moving the package to another machine.

The package includes:

- `setup.exe` for selecting the model root.
- `imageColorer.exe` for preparing a copied model.
- `picker.exe` for locating texture parts.

After copying the package to another Windows machine, run `setup.exe` there and
select the copied model folder before using the other programs.
