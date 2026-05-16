/*
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 */

#include "assembly_step.h"
#include "exporter_step.h"

#include <reporter.h>
#include <wx/app.h>
#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

#include <BRep_Builder.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <STEPControl_Reader.hxx>
#include <STEPControl_StepModelType.hxx>
#include <STEPControl_Writer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <cmath>


namespace
{

constexpr double DEG2RAD = 0.017453292519943295;  // π / 180


/// Build the per-board world-space transform: T(position) · R_z · R_y · R_x.
/// Mirrors `BOARD_3D_INSTANCE::GetTransformMatrix` so STEP placement matches
/// what the user sees in the OpenGL viewer.
gp_Trsf makeBoardTrsf( const VECTOR3D& aPositionMm, const VECTOR3D& aRotationDeg )
{
    gp_Trsf rotX;
    gp_Trsf rotY;
    gp_Trsf rotZ;
    gp_Trsf trans;

    rotX.SetRotation( gp_Ax1( gp_Pnt( 0.0, 0.0, 0.0 ), gp_Dir( 1.0, 0.0, 0.0 ) ),
                      aRotationDeg.x * DEG2RAD );
    rotY.SetRotation( gp_Ax1( gp_Pnt( 0.0, 0.0, 0.0 ), gp_Dir( 0.0, 1.0, 0.0 ) ),
                      aRotationDeg.y * DEG2RAD );
    rotZ.SetRotation( gp_Ax1( gp_Pnt( 0.0, 0.0, 0.0 ), gp_Dir( 0.0, 0.0, 1.0 ) ),
                      aRotationDeg.z * DEG2RAD );
    trans.SetTranslation( gp_Vec( aPositionMm.x, aPositionMm.y, aPositionMm.z ) );

    // glm composition order in `BOARD_3D_INSTANCE::GetTransformMatrix` is
    // T · R_z · R_y · R_x; gp_Trsf concatenation right-multiplies so the
    // first-applied rotation ends up at the right.
    gp_Trsf result = trans;
    result.Multiply( rotZ );
    result.Multiply( rotY );
    result.Multiply( rotX );
    return result;
}


/// Sanitize a board display name for filesystem use (temp filename).
wxString sanitizeForFilename( const wxString& aName )
{
    wxString out;
    out.reserve( aName.length() );

    for( wxUniChar c : aName )
    {
        if( wxIsalnum( c ) || c == '_' || c == '-' )
            out.Append( c );
        else
            out.Append( '_' );
    }

    if( out.IsEmpty() )
        out = wxT( "board" );

    return out;
}

}  // namespace


bool ExportPcbAssemblyToSTEP( const std::vector<ASSEMBLY_STEP_BOARD>& aBoards,
                              const wxString&                         aOutputFile,
                              REPORTER*                               aReporter )
{
    REPORTER* reporter = aReporter ? aReporter : &NULL_REPORTER::GetInstance();

    if( aBoards.empty() )
    {
        reporter->Report( _( "No boards in assembly to export." ), RPT_SEVERITY_ERROR );
        return false;
    }

    // Per-board export creates one STEP file in this scratch directory;
    // we then re-read each one and stitch them into the assembly compound.
    wxFileName tempDir;
    tempDir.AssignDir( wxStandardPaths::Get().GetTempDir() );
    tempDir.AppendDir( wxString::Format( wxT( "kicad_assembly_step_%lld_%u" ),
                                         static_cast<long long>( ::time( nullptr ) ),
                                         ::wxGetProcessId() ) );

    if( !tempDir.Mkdir( wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL ) )
    {
        reporter->Report( wxString::Format( _( "Could not create temp directory '%s'." ),
                                            tempDir.GetFullPath() ),
                          RPT_SEVERITY_ERROR );
        return false;
    }

    // Let the caller's busy/progress dialog paint before we start
    // blocking on per-board OCCT work.
    if( wxTheApp )
        wxTheApp->Yield( /*onlyIfNeeded=*/true );

    struct PerBoardArtifact
    {
        wxString tempFile;
        VECTOR3D positionMm;
        VECTOR3D rotationDeg;
        wxString name;
    };

    std::vector<PerBoardArtifact> artifacts;
    artifacts.reserve( aBoards.size() );

    for( size_t i = 0; i < aBoards.size(); ++i )
    {
        const ASSEMBLY_STEP_BOARD& entry = aBoards[i];

        if( !entry.board )
            continue;

        EXPORTER_STEP_PARAMS params;
        params.m_Format = EXPORTER_STEP_PARAMS::FORMAT::STEP;
        params.m_Overwrite = true;

        wxFileName perBoardFile = tempDir;
        perBoardFile.SetFullName(
                wxString::Format( wxT( "%s_%zu.step" ), sanitizeForFilename( entry.name ), i ) );

        EXPORTER_STEP exporter( entry.board, params, reporter );
        exporter.m_outputFile = perBoardFile.GetFullPath();

        reporter->Report( wxString::Format( _( "Exporting board '%s' (%zu of %zu)..." ),
                                            entry.name, i + 1, aBoards.size() ),
                          RPT_SEVERITY_INFO );

        if( !exporter.Export() )
        {
            reporter->Report( wxString::Format( _( "Per-board STEP export failed for '%s'; "
                                                   "skipping this board in the assembly." ),
                                                entry.name ),
                              RPT_SEVERITY_WARNING );
            continue;
        }

        // Pump events between per-board exports so the UI's busy
        // indicator remains responsive instead of looking frozen for
        // the duration of all boards combined.
        if( wxTheApp )
            wxTheApp->Yield( /*onlyIfNeeded=*/true );

        PerBoardArtifact a;
        a.tempFile    = perBoardFile.GetFullPath();
        a.positionMm  = entry.positionMm;
        a.rotationDeg = entry.rotationDeg;
        a.name        = entry.name;
        artifacts.push_back( std::move( a ) );
    }

    auto cleanupTempDir = [&]()
    {
        for( const PerBoardArtifact& a : artifacts )
            ::wxRemoveFile( a.tempFile );
        tempDir.Rmdir( wxPATH_RMDIR_RECURSIVE );
    };

    if( artifacts.empty() )
    {
        reporter->Report( _( "No board exports succeeded — assembly STEP not written." ),
                          RPT_SEVERITY_ERROR );
        cleanupTempDir();
        return false;
    }

    BRep_Builder    builder;
    TopoDS_Compound compound;
    builder.MakeCompound( compound );

    for( const PerBoardArtifact& a : artifacts )
    {
        STEPControl_Reader reader;

        IFSelect_ReturnStatus readStatus =
                reader.ReadFile( static_cast<const char*>( a.tempFile.mb_str() ) );

        if( readStatus != IFSelect_RetDone )
        {
            reporter->Report( wxString::Format( _( "Could not re-read per-board STEP for '%s'; "
                                                   "skipping." ),
                                                a.name ),
                              RPT_SEVERITY_WARNING );
            continue;
        }

        reader.TransferRoots();
        TopoDS_Shape shape = reader.OneShape();

        if( shape.IsNull() )
        {
            reporter->Report( wxString::Format( _( "Re-read of '%s' produced an empty shape; "
                                                   "skipping." ),
                                                a.name ),
                              RPT_SEVERITY_WARNING );
            continue;
        }

        TopLoc_Location loc( makeBoardTrsf( a.positionMm, a.rotationDeg ) );
        builder.Add( compound, shape.Located( loc ) );
    }

    STEPControl_Writer writer;

    if( writer.Transfer( compound, STEPControl_AsIs ) != IFSelect_RetDone )
    {
        reporter->Report( _( "OpenCASCADE failed to transfer assembly compound to STEP." ),
                          RPT_SEVERITY_ERROR );
        cleanupTempDir();
        return false;
    }

    IFSelect_ReturnStatus writeStatus =
            writer.Write( static_cast<const char*>( aOutputFile.mb_str() ) );

    cleanupTempDir();

    if( writeStatus != IFSelect_RetDone )
    {
        reporter->Report( wxString::Format( _( "Failed to write assembly STEP '%s'." ),
                                            aOutputFile ),
                          RPT_SEVERITY_ERROR );
        return false;
    }

    reporter->Report( wxString::Format( _( "Wrote assembly STEP with %zu board(s) to '%s'." ),
                                        artifacts.size(), aOutputFile ),
                      RPT_SEVERITY_INFO );
    return true;
}
