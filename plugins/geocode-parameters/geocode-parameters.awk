#!/usr/bin/awk -f
#
## @file
## @brief Used by the Search option "search on geo-position".
##
## It is used to decode the results of internet or other searches
## to extract a geo-position from a text string.
##
## To include other searches, follow the examples below and
## store the file in:  
## ~/.config/geeqie/applications/geocode-parameters.awk  
## Ensure the returned value is either in the format:  
## 89.123 179.123  
## or  
## Error: $0  
##

function check_parameters(latitude, longitude)
    {
    # Ensure the parameters are numbers
    if ((latitude == (latitude+0)) && (longitude == (longitude+0)))
        {
        if (latitude >= -90 && latitude <= 90 &&
                        longitude >= -180 && longitude <= 180)
            {
            return latitude " " longitude
            }
        else
            {
            return "Error: " latitude " " longitude
            }
        }
    else
        {
        return "Error: " latitude " " longitude
        }
    }

# This awk file is accessed by the decode_geo_parameters() function
# in search.cc. The call is of the format:
# echo "string_to_be_searched" | awk -f geocode-parameters.awk
#
# Search the input string for known formats.
{
if (index($0, "https://www.geonames.org/maps/google_"))
    {
    # This is a drag-and-drop or copy-paste from a geonames.org search
    # in the format e.g.
    # https://www.geonames.org/maps/google_51.513_-0.092.html

    gsub(/https:\/\/www.geonames.org\/maps\/google_/, "")
    gsub(/.html/, "")
    gsub(/_/, " ")
    print check_parameters($1, $2)
    }

else if (index($0, "https://www.openstreetmap.org/search?query="))
    {
    # This is a copy-paste from an openstreetmap.org search
    # in the format e.g.
    # https://www.openstreetmap.org/search?query=51.4878%2C-0.1353#map=11/51.4880/-0.1356

    gsub(/https:\/\/www.openstreetmap.org\/search\?query=/, "")
    gsub(/#map=.*/, "")
    gsub(/%2C/, " ")
    print check_parameters($1, $2)
    }

else if (index($0, "https://www.openstreetmap.org/#map="))
    {
    # This is a copy-paste from an openstreetmap.org search
    # in the format e.g.
    # https://www.openstreetmap.org/#map=5/18.271/16.084

    gsub(/https:\/\/www.openstreetmap.org\/#map=[^\/]*/,"")
    gsub(/\//," ")
    print check_parameters($1, $2)
    }

else if (index($0, "https://www.google.com/maps/"))
    {
    # This is a copy-paste from a google.com maps search
    # in the format e.g.
    # https://www.google.com/maps/place/London,+UK/@51.5283064,-0.3824815,10z/data=....

    gsub(/https:\/\/www.google.com\/maps.*@/,"")
    sub(/,/," ")
    gsub(/,.*/,"")
    print check_parameters($1, $2)
    }

else if (index($0,".html"))
    {
    # This is an unknown html address

    print "Error: " $0
    }

else if (index($0,"http"))
    {
    # This is an unknown html address

    print "Error: " $0
    }

else if (index($0, ","))
    {
    # This is assumed to be a simple lat/long of the format:
    # 89.123,179.123

    split($0, latlong, ",")
    print check_parameters(latlong[1], latlong[2])
    }

else
    {
    # This is assumed to be a simple lat/long of the format:
    # 89.123 179.123

    split($0, latlong, " ")
    print check_parameters(latlong[1], latlong[2])
    }
}
