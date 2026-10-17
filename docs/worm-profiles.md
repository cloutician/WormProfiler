# Worm Profiles and Tissue Measurements

## Input from ilastik

Use ilastik's **Simple Segmentation** export: each pixel contains a tissue-class label, not a probability or display colour. See the [ilastik export documentation](https://www.ilastik.org/documentation/basics/export.html).

The initial importer accepts one 2D segmentation per TIFF: single-page, single-channel unsigned 8-bit or 16-bit data. Floating-point and multichannel images, multi-page stacks, unreadable files, and images with more than 256 distinct label values are rejected with an explanation. No page, class order, probability threshold, or stack projection is silently assumed.

## Create a profile

1. Open **Worm profiles** and choose **New profile**. Enter an identifier for the individual worm.
2. Select **Add TIFFs**, choose one or more segmentation files, then choose their observation date with the calendar. A batch receives the same initial date.
3. Select individual observations to change their dates or add notes. The list and measurement table stay chronological. Multiple images on the same date remain separate observations; their areas are not added together.
4. Open **Tissue classes**. Assign every detected TIFF label to Donor, Host, Background, Eyes, or Ignore. Multiple label values can map to the same tissue. Label 0 is not automatically assumed to be background.
5. Check the colour-coded segmentation and region areas. The **Measurements** tab shows the observations together.
6. Save the profile as a `.wormprofile.json` file, then use **Export CSV**.

The mapping is shared by the observations in a profile. Use TIFFs with consistent class meanings. Importing a new label value makes export unavailable until that value is explicitly assigned. Duplicate source files are skipped; separate files from the same date are retained.

## Areas and scale

Pixel areas are exact counts of the original label pixels. The measurement importer does not apply image-preparation adjustments, rescale labels, remove small components, or alter the TIFFs.

Use one worm's segmentation per TIFF. All classified pixels in an image are counted, including disconnected regions and both eyes; assigning a worm ID does not automatically separate multiple worms in the same image.

For calibrated measurements, enter the physical pixel **width** and **height** in micrometres for each observation. Both must be positive. A region's area is:

```text
area_um2 = region_pixel_count * pixel_width_um * pixel_height_um
```

Leave both dimensions at **Unknown** for pixel-only measurements. Calibrated CSV columns are then empty, not zero. Scale is entered explicitly; TIFF display DPI is not used as a microscopy calibration. If an image was resized before segmentation, use the pixel scale of the segmented image, not the original camera image.

`worm_px` is Donor + Host + Eyes. Background and Ignore are excluded. `donor_fraction_of_tissue` is Donor / (Donor + Host), excluding Eyes; it is empty if both tissue counts are zero. These are defined summary quantities, not a claim that growth is comparable without consistent imaging, segmentation, and calibration.

## CSV contents

CSV export writes UTF-8 with a byte-order mark for spreadsheet compatibility, ISO dates, decimal points, and one row per observation, sorted by date and source path.

| Columns | Meaning |
| --- | --- |
| `worm_id`, `observation_id` | Worm identifier and stable observation identifier |
| `date`, `days_since_first_observation` | Assigned observation date and elapsed whole days from the earliest date in the profile |
| `image_file`, `image_path`, `sha256` | Source TIFF identity and SHA-256 hash of the measured bytes |
| `width_px`, `height_px` | Segmentation dimensions |
| `donor_px`, `host_px`, `background_px`, `eyes_px` | Tissue-class pixel areas |
| `ignored_px`, `unmapped_px`, `worm_px` | Explicitly ignored pixels, unmapped pixels, and foreground summary |
| `donor_fraction_of_tissue` | Donor / (Donor + Host), from 0 to 1 when defined |
| `pixel_width_um`, `pixel_height_um` | Explicit per-observation scale, or empty when unknown |
| `donor_um2`, `host_um2`, `background_um2`, `eyes_um2` | Calibrated areas, or empty when unknown |
| `observation_notes`, `profile_notes`, `label_mapping` | Recorded context and reproducible class assignments |

TIFFs are re-read before export; missing, changed, unsupported, or incompletely mapped inputs cannot silently reuse old measurements. A changed but valid TIFF is measured again. A failed validation or cancellation leaves any existing CSV untouched. Exports are written atomically. Editable text that could be interpreted as a spreadsheet formula receives a protective leading apostrophe.

## Saving and reopening

Profile files retain the worm ID, notes, observation identifiers, dates, source paths, class mapping, and pixel scales. They reference TIFFs rather than embedding copies or cached measurement results. Paths are relative to the profile where possible, so the profile and images can be moved together while keeping their folder layout. Reopening re-reads the TIFFs in the background.

Save profiles before closing. New/Open and window close protect unsaved changes. Missing inputs remain listed with an error; restore the file at its saved path, reload measurements, or remove the observation and import its replacement. Removing an observation never deletes its TIFF.

## Verification

The automated suite checks exact 8-bit and 16-bit label counts, nonstandard label IDs, multiple labels per tissue, Ignore and unmapped pixels, probability/colour/stack rejection, profile validation and relative paths, date ordering, calibration, CSV escaping, source-file protection, background import/export, changed-source validation, calendar popups, saved-profile reopening, and unsaved-change prompts. Screenshots use synthetic label masks, not experimental data.
