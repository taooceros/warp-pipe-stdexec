package("doca-stdexec")
    set_kind("library", {headeronly = true})
    set_description("The doca-stdexec package")
    add_deps("pkgconfig::doca-argp")
    add_deps("pkgconfig::doca-aes-gcm")
    add_deps("pkgconfig::doca-comch")
    add_deps("pkgconfig::doca-common")
    add_deps("pkgconfig::doca-dma")
    add_deps("pkgconfig::doca-rdma")
    add_deps("pkgconfig::doca-sha")
    add_deps("stdexec main")

    add_urls("https://github.com/taooceros/doca-stdexec.git")

    on_install(function (package)
        local configs = {}
        if package:config("shared") then
            configs.kind = "shared"
        end
        import("package.tools.xmake").install(package, configs)
        print(os.files("**.hpp"))
        print(os.files(package:installdir("include/")))
        os.cp("include", package:installdir())
        return configs
    end)


    on_test(function (package)
        -- TODO check includes and interfaces
        print(os.files(package:installdir("include/")))
        assert(package:has_cxxincludes("doca_stdexec/buf_inventory.hpp", {configs = {languages = "c++23"}}))
    end)
