#!/bin/bash
TS=`pwd`/themestrings

pushd ..
    chmod a-x mythplugins
popd > /dev/null

pushd ../mythtv/themes
    $TS ../.. . > /dev/null
popd > /dev/null

pushd ..
    chmod a+x mythplugins
popd > /dev/null

# exclude mythweather us_nws strings
chmod a-r ../mythplugins/mythweather/mythweather/scripts/us_nws/maps.xml

for I in `ls ../mythplugins --file-type | grep "/$" | grep -v cleanup | grep -v mythweb` ; do
    pushd ../mythplugins/$I
    $TS . `pwd`/i18n > /dev/null
    popd > /dev/null
done

chmod a+r ../mythplugins/mythweather/mythweather/scripts/us_nws/maps.xml

pushd .. > /dev/null
    svn st
    emacs `svn st | grep "^M" | sed -e "s/^M//"` &
popd > /dev/null