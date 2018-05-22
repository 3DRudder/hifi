#
#  Created Nicolas PERRET 17/04/2018
#
#  Distributed under the Apache License, Version 2.0.
#  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
#
macro(TARGET_3DRUDDERSDK)
    # 3dRudder SDK data reader is only available on these platforms
    if (WIN32)
        add_dependency_external_projects(3dRudderSDK)
        target_include_directories(${TARGET_NAME} PRIVATE ${3DRUDDERSDK_INCLUDE_DIRS})
        target_link_libraries(${TARGET_NAME} ${3DRUDDERSDK_LIBRARIES})
        add_definitions(-DHAVE_3DRUDDERSDK)
    endif(WIN32)
endmacro()
