vcpkg_download_distfile(ONNXRUNTIME_ARCHIVE
    URLS "https://www.nuget.org/api/v2/package/Microsoft.ML.OnnxRuntime.DirectML/${VERSION}"
    FILENAME "microsoft.ml.onnxruntime.directml.${VERSION}.nupkg.zip"
    SHA512 6633b2bf8f79be17d55e84c9a76bad4729fc8abd53148bc28f407d24ff106460574b4a05c7e958ed9099a927675528505fa12dc588b64f754eb585dc9814d5e0
)
vcpkg_download_distfile(DIRECTML_ARCHIVE
    URLS "https://www.nuget.org/api/v2/package/Microsoft.AI.DirectML/1.15.4"
    FILENAME "microsoft.ai.directml.1.15.4.nupkg.zip"
    SHA512 fde767f56904abc90fd53f65d8729c918ab7f6e3c5e1ecdd479908fc02b4535cf2b0860f7ab2acb9b731d6cb809b72c3d5d4d02853fb8f5ea022a47bc44ef285
)

vcpkg_extract_source_archive(ONNXRUNTIME_SOURCE
    ARCHIVE "${ONNXRUNTIME_ARCHIVE}"
    NO_REMOVE_ONE_LEVEL
)
vcpkg_extract_source_archive(DIRECTML_SOURCE
    ARCHIVE "${DIRECTML_ARCHIVE}"
    NO_REMOVE_ONE_LEVEL
)

file(INSTALL "${ONNXRUNTIME_SOURCE}/build/native/include/"
    DESTINATION "${CURRENT_PACKAGES_DIR}/include"
)
file(INSTALL "${DIRECTML_SOURCE}/include/"
    DESTINATION "${CURRENT_PACKAGES_DIR}/include"
)

foreach(CONFIG_PREFIX "" "debug/")
    file(INSTALL "${ONNXRUNTIME_SOURCE}/runtimes/win-x64/native/onnxruntime.lib"
        DESTINATION "${CURRENT_PACKAGES_DIR}/${CONFIG_PREFIX}lib"
    )
    file(INSTALL "${DIRECTML_SOURCE}/bin/x64-win/DirectML.lib"
        DESTINATION "${CURRENT_PACKAGES_DIR}/${CONFIG_PREFIX}lib"
    )
    file(INSTALL "${ONNXRUNTIME_SOURCE}/runtimes/win-x64/native/onnxruntime.dll"
        "${ONNXRUNTIME_SOURCE}/runtimes/win-x64/native/onnxruntime_providers_shared.dll"
        "${DIRECTML_SOURCE}/bin/x64-win/DirectML.dll"
        DESTINATION "${CURRENT_PACKAGES_DIR}/${CONFIG_PREFIX}bin"
    )
endforeach()

vcpkg_install_copyright(FILE_LIST "${ONNXRUNTIME_SOURCE}/LICENSE")
