/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#ifndef ASSEMBLY_STEP_H
#define ASSEMBLY_STEP_H

#include <math/vector3.h>
#include <vector>
#include <wx/string.h>


class BOARD;
class REPORTER;


/**
 * One BOARD in a multi-board STEP assembly export, paired with its
 * world-space pose. M6.F deliverable.
 */
struct ASSEMBLY_STEP_BOARD
{
    BOARD*   board;        ///< Source BOARD; must have its project set so 3D
                           ///< model paths (KIPRJMOD-relative) resolve.
    wxString name;         ///< Display label used as the per-board STEP
                           ///< product name (visible as a tree item in CAD
                           ///< tools).
    VECTOR3D positionMm;   ///< World translation in mm.
    VECTOR3D rotationDeg;  ///< Euler rotation in degrees, applied Z-Y-X
                           ///< (matching `BOARD_3D_INSTANCE::GetTransformMatrix`).
};


/**
 * Export several BOARDs into a single STEP file, each positioned by
 * its `positionMm` / `rotationDeg`. The implementation runs the
 * standard per-board STEP exporter (`EXPORTER_STEP::Export`) into a
 * temp directory, reads each output back, applies the per-board
 * transform via `TopLoc_Location`, and writes a single compound STEP.
 *
 * Returns true on success, false if any irrecoverable step failed
 * (no boards exported, write failure). Per-board export failures are
 * non-fatal: that board is skipped and the rest of the assembly is
 * still written.
 *
 * @param aBoards        Boards + per-board poses. Order is preserved.
 * @param aOutputFile    Destination STEP file path.
 * @param aReporter      Optional progress / error reporter; pass
 *                       nullptr for the global NULL_REPORTER.
 */
bool ExportPcbAssemblyToSTEP( const std::vector<ASSEMBLY_STEP_BOARD>& aBoards,
                              const wxString&                         aOutputFile,
                              REPORTER*                               aReporter = nullptr );

#endif // ASSEMBLY_STEP_H
