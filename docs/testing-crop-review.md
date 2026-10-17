# Desktop Workflow: Review and Testing

## Image preparation

1. Use **File > Open folder**. Images should appear in the left browser, with the selected image shown in the original and processed views.
2. Change intensity, smoothing, and geometry settings in the right inspector. Check that numeric values and sliders stay in sync and the processed preview changes.
3. Scroll over either image to zoom and drag to pan. Use Fit or Actual size in the toolbar or View menu to reset the views.
4. Filter the image list, navigate between files, and open another folder. The selected filename and displayed image must stay in sync.
5. Load a matching mask. Toggle the overlay and change its opacity; the right view should identify itself as Mask overlay. Image export still exports the processed image.
6. Resize the window and drag the pane dividers. The View menu can hide the browser or properties inspector.

Switch between Image preparation and Crop review during the checks below. Image settings and review decisions should remain available in their own workspace.

## Start a review

1. Use **Review > New review**, or New review in the Crop review toolbar. Choose the image folder, ONNX model, margin, and threshold, then select Run model.
2. Watch progress in the main status bar. The window should remain responsive while predictions run. Completed predictions should appear in Crop review inside the same window.
3. Select images in the thumbnail list. Confirm that the filename, dimensions, crop preview, and prediction message match the selected image.
4. Drag and resize a crop, then check that the X, Y, Width, and Height fields follow the change. Scroll to zoom; drag with the middle mouse button to pan. Coordinate fields accept crops down to one pixel.
5. Mark several images **Approve**, **Reject**, or **Flag**. Use Clear decision to return an image to Pending. Decision controls should remain visible while scrolling the properties inspector.
6. Try each filter. Previous and Next navigate only visible images. Advance after decision moves to the next visible image when available.
7. Adjust an approved crop. Its decision should return to Pending and the approved count should decrease.
8. Use Open source in the toolbar. It should open the original image in Image preparation without changing the crop decision.

Images with no successful model prediction initially have Needs review status. Their warning appears in the crop properties inspector. You can manually correct and approve the fallback crop after inspecting it.

## Save and resume

1. Use **Save session** to create a `.wormreview.json` file.
2. Use **Review > Open session** to reopen the file, or close and restart the application first.
3. Confirm that the crops, decisions, model information, and current image were restored without running inference.
4. Make another change and close the main window. Test Cancel, Save, and Discard in the save prompt. Starting or opening another review must also protect unsaved decisions.

Sessions reference the original image files; they do not embed image copies. Keep the images available. Moving the session and images together while retaining their relative folder layout keeps the links working. Missing images or changed image dimensions are shown as warnings and cannot be newly approved.

## Export

1. Approve at least one image and leave others Pending, Rejected, or Needs review.
2. Select **Export approved** and an output folder. Only Approved images should have PNG crops.
3. Check crop dimensions and confirm the original images were not changed. Crop export preserves the source pixels and bit depth; it does not apply the main window's preprocessing settings.
4. Open `crop_manifest.csv`. It includes every image's decision, crop coordinates, model path and threshold, and export result. Failed or canceled approved exports are identified separately.
5. Export to the same folder again. New filenames receive numeric suffixes; existing PNGs and manifests should remain unchanged.
6. Try canceling an export with the cancel control in the main status bar. Finished crops are retained and the manifest records which approved images were not exported.

## Batch processing

1. Set a representative image's adjustments, then choose Batch export.
2. Confirm the input and output folders in the setup dialog and start the run.
3. Change an adjustment while the run is active. The outputs must use the settings captured when the job started.
4. Check that a JSON preprocessing profile is saved beside the outputs and that running again preserves previous files with numbered names.
5. Cancel a run. The current image may finish before cancellation takes effect. Inspect any failure details and confirm the original inputs are unchanged.

## Preprocessing profiles

1. Change several preprocessing settings and use **Profiles > Save profile** or the Processing menu.
2. Change the controls again, then use Load profile to restore the JSON file.
3. Check that every control and the processed preview return to the saved settings.
4. An unrelated JSON file, invalid settings, or a legacy text profile should show an error without changing the current settings.

## Automated checks

The CMake test target `WormProfilerTests` covers JSON validation and round trips, integrated workspace state, folder changes, unreadable images, zoomed crop coordinates, background processing, approval invalidation, unsaved-change prompts, cancellation, export collisions, CSV escaping, and preservation of 16-bit crop pixels.

The desktop-control checks also cover slider keyboard input, track clicks and dragging, independent checkboxes, exclusive radio buttons, spinbox steppers, dropdown keyboard input, and nonempty vector icons at 100%, 150%, and 200% scale. Setting `WORMPROFILER_TEST_ARTIFACT_DIR` captures both workspaces at several window sizes and a control-state preview.

For a visual check on Windows, also try Display scaling at 100%, 150%, and 200%. Look for crisp icons, readable numeric suffixes, visible keyboard focus, muted disabled controls, and decision buttons that remain accessible at a 960 x 640 window size.

Configure with `BUILD_TESTING=ON` and Qt Test available, then build and run CTest. Set the Qt and OpenCV runtime folders on PATH when running tests on Windows. The tests use Qt's offscreen platform and do not need an ONNX model or your microscopy data.
