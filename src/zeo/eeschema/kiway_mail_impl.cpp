void SCH_EDIT_FRAME::KiwayMailIn( KIWAY_EXPRESS& aEvent )
{
    const std::string& payload = aEvent.Payload();

    if( aEvent.Command() == MAIL_AGENT_REQUEST )
    {
        std::string    request = payload;
        nlohmann::json response;

        if( request == "GET_SCH_INFO" )
        {
            response["items"] = nlohmann::json::array();
            for( SCH_ITEM* item : Schematic().Root().GetScreen()->Items() )
            {
                nlohmann::json jItem;
                jItem["type"] = item->GetClass();
                jItem["uuid"] = item->m_Uuid.AsString().ToStdString();
                response["items"].push_back( jItem );
            }
        }
        else if( request == "GET_CONNECTION_INFO" )
        {
            // TODO: Serialize Connection Graph
            response["nets"] = "Not implemented full serialization yet";
        }
        else
        {
            response["error"] = "Unknown command: " + request;
        }

        std::string responseStr = response.dump();
        Kiway().ExpressMail( FRAME_AGENT, MAIL_AGENT_RESPONSE, responseStr, this );
        return;
    }

    SCH_BASE_FRAME::KiwayMailIn( aEvent );
}
