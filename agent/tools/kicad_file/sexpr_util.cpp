#include "sexpr_util.h"
#include <sstream>
#include <sexpr/sexpr_exception.h>

namespace SexprUtil
{

ValidationResult ValidateSyntax( const std::string& aContent )
{
    try
    {
        SEXPR::PARSER parser;
        auto parsed = parser.Parse( aContent );
        if( !parsed )
            return ValidationResult( "Failed to parse S-expression" );
        return ValidationResult();
    }
    catch( const SEXPR::PARSE_EXCEPTION& e )
    {
        return ValidationResult( e.what() );
    }
    catch( const std::exception& e )
    {
        return ValidationResult( e.what() );
    }
}


std::unique_ptr<SEXPR::SEXPR> Parse( const std::string& aContent )
{
    try
    {
        SEXPR::PARSER parser;
        return parser.Parse( aContent );
    }
    catch( ... )
    {
        return nullptr;
    }
}


std::unique_ptr<SEXPR::SEXPR> ParseFile( const std::string& aFilePath )
{
    try
    {
        SEXPR::PARSER parser;
        return parser.ParseFromFile( aFilePath );
    }
    catch( ... )
    {
        return nullptr;
    }
}


std::string GetListType( const SEXPR::SEXPR* aSexpr )
{
    if( !aSexpr || !aSexpr->IsList() )
        return "";

    auto children = aSexpr->GetChildren();
    if( !children || children->empty() )
        return "";

    const SEXPR::SEXPR* first = children->at( 0 );
    if( first->IsSymbol() )
        return first->GetSymbol();

    return "";
}


std::vector<const SEXPR::SEXPR*> FindChildren( const SEXPR::SEXPR* aSexpr, const std::string& aType )
{
    std::vector<const SEXPR::SEXPR*> result;

    if( !aSexpr || !aSexpr->IsList() )
        return result;

    auto children = aSexpr->GetChildren();
    if( !children )
        return result;

    for( const auto& child : *children )
    {
        if( child->IsList() && GetListType( child ) == aType )
            result.push_back( child );
    }

    return result;
}


const SEXPR::SEXPR* FindFirstChild( const SEXPR::SEXPR* aSexpr, const std::string& aType )
{
    if( !aSexpr || !aSexpr->IsList() )
        return nullptr;

    auto children = aSexpr->GetChildren();
    if( !children )
        return nullptr;

    for( const auto& child : *children )
    {
        if( child->IsList() && GetListType( child ) == aType )
            return child;
    }

    return nullptr;
}


std::string GetStringValue( const SEXPR::SEXPR* aSexpr )
{
    if( !aSexpr || !aSexpr->IsList() )
        return "";

    auto children = aSexpr->GetChildren();
    if( !children || children->size() < 2 )
        return "";

    const SEXPR::SEXPR* value = children->at( 1 );
    if( value->IsString() )
        return value->GetString();
    if( value->IsSymbol() )
        return value->GetSymbol();

    return "";
}


int GetIntValue( const SEXPR::SEXPR* aSexpr )
{
    if( !aSexpr || !aSexpr->IsList() )
        return 0;

    auto children = aSexpr->GetChildren();
    if( !children || children->size() < 2 )
        return 0;

    const SEXPR::SEXPR* value = children->at( 1 );
    if( value->IsInteger() )
        return static_cast<int>( value->GetInteger() );

    return 0;
}


double GetDoubleValue( const SEXPR::SEXPR* aSexpr )
{
    if( !aSexpr || !aSexpr->IsList() )
        return 0.0;

    auto children = aSexpr->GetChildren();
    if( !children || children->size() < 2 )
        return 0.0;

    const SEXPR::SEXPR* value = children->at( 1 );
    if( value->IsDouble() )
        return value->GetDouble();
    if( value->IsInteger() )
        return static_cast<double>( value->GetInteger() );

    return 0.0;
}


bool GetCoordinates( const SEXPR::SEXPR* aSexpr, double& aX, double& aY, double& aAngle )
{
    aX = 0.0;
    aY = 0.0;
    aAngle = 0.0;

    if( !aSexpr || !aSexpr->IsList() )
        return false;

    auto children = aSexpr->GetChildren();
    if( !children || children->size() < 3 )
        return false;

    // Skip the 'at' symbol
    const SEXPR::SEXPR* xExpr = children->at( 1 );
    const SEXPR::SEXPR* yExpr = children->at( 2 );

    if( xExpr->IsDouble() )
        aX = xExpr->GetDouble();
    else if( xExpr->IsInteger() )
        aX = static_cast<double>( xExpr->GetInteger() );
    else
        return false;

    if( yExpr->IsDouble() )
        aY = yExpr->GetDouble();
    else if( yExpr->IsInteger() )
        aY = static_cast<double>( yExpr->GetInteger() );
    else
        return false;

    // Optional angle
    if( children->size() >= 4 )
    {
        const SEXPR::SEXPR* angleExpr = children->at( 3 );
        if( angleExpr->IsDouble() )
            aAngle = angleExpr->GetDouble();
        else if( angleExpr->IsInteger() )
            aAngle = static_cast<double>( angleExpr->GetInteger() );
    }

    return true;
}


std::string ToString( const SEXPR::SEXPR* aSexpr, int aIndentLevel )
{
    if( !aSexpr )
        return "";

    // Use the built-in AsString method
    return aSexpr->AsString( aIndentLevel );
}

} // namespace SexprUtil
