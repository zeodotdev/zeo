#include "create_project_handler.h"
#include <nlohmann/json.hpp>
#include <wx/dir.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <kiid.h>


std::string CREATE_PROJECT_HANDLER::Execute( const std::string& aToolName,
                                              const nlohmann::json& aInput )
{
    std::string projectName = aInput.value( "project_name", "" );
    std::string directory = aInput.value( "directory", "" );

    if( projectName.empty() || directory.empty() )
        return "Error: project_name and directory are required";

    // Create project directory
    wxString projDir = wxString::FromUTF8( directory ) + wxFileName::GetPathSeparator() +
                       wxString::FromUTF8( projectName );

    if( !wxDir::Make( projDir, wxS_DIR_DEFAULT ) && !wxDir::Exists( projDir ) )
        return "Error: Could not create project directory: " + projDir.ToStdString();

    wxString basePath = projDir + wxFileName::GetPathSeparator() + wxString::FromUTF8( projectName );

    // Create minimal .kicad_pro file
    wxString proFile = basePath + ".kicad_pro";
    {
        wxFile f( proFile, wxFile::write );
        if( f.IsOpened() )
        {
            nlohmann::json proJson = {
                { "meta", { { "filename", projectName + ".kicad_pro" }, { "version", 1 } } },
                { "schematic", { { "legacy_lib_dir", "" }, { "legacy_lib_list", nlohmann::json::array() } } }
            };
            f.Write( wxString::FromUTF8( proJson.dump( 2 ) ) );
        }
    }

    // Create minimal .kicad_sch file
    wxString schFile = basePath + ".kicad_sch";
    {
        wxFile f( schFile, wxFile::write );
        if( f.IsOpened() )
        {
            f.Write(
                "(kicad_sch\n"
                "  (version 20250114)\n"
                "  (generator \"zeo_agent\")\n"
                "  (generator_version \"1.0\")\n"
                "  (uuid \"" + KIID().AsStdString() + "\")\n"
                "  (paper \"A4\")\n"
                "  (lib_symbols)\n"
                "  (sheet_instances\n"
                "    (path \"/\" (page \"\"))\n"
                "  )\n"
                ")\n"
            );
        }
    }

    // Create minimal .kicad_pcb file
    wxString pcbFile = basePath + ".kicad_pcb";
    {
        wxFile f( pcbFile, wxFile::write );
        if( f.IsOpened() )
        {
            f.Write(
                "(kicad_pcb\n"
                "  (version 20250114)\n"
                "  (generator \"zeo_agent\")\n"
                "  (generator_version \"1.0\")\n"
                "  (general\n"
                "    (thickness 1.6)\n"
                "    (legacy_teardrops no)\n"
                "  )\n"
                "  (paper \"A4\")\n"
                "  (layers\n"
                "    (0 \"F.Cu\" signal)\n"
                "    (31 \"B.Cu\" signal)\n"
                "    (32 \"B.Adhes\" user \"B.Adhesive\")\n"
                "    (33 \"F.Adhes\" user \"F.Adhesive\")\n"
                "    (34 \"B.Paste\" user)\n"
                "    (35 \"F.Paste\" user)\n"
                "    (36 \"B.SilkS\" user \"B.Silkscreen\")\n"
                "    (37 \"F.SilkS\" user \"F.Silkscreen\")\n"
                "    (38 \"B.Mask\" user)\n"
                "    (39 \"F.Mask\" user)\n"
                "    (40 \"Dwgs.User\" user \"User.Drawings\")\n"
                "    (44 \"Edge.Cuts\" user)\n"
                "  )\n"
                "  (setup\n"
                "    (pad_to_mask_clearance 0)\n"
                "  )\n"
                ")\n"
            );
        }
    }

    nlohmann::json result = {
        { "status", "success" },
        { "project_path", projDir.ToStdString() },
        { "files_created", {
            projectName + ".kicad_pro",
            projectName + ".kicad_sch",
            projectName + ".kicad_pcb"
        }}
    };

    wxLogInfo( "CREATE_PROJECT_HANDLER: Created project '%s' at '%s'",
               projectName, projDir.ToStdString() );

    return result.dump( 2 );
}


std::string CREATE_PROJECT_HANDLER::GetDescription( const std::string& aToolName,
                                                     const nlohmann::json& aInput ) const
{
    return "Create Project";
}
