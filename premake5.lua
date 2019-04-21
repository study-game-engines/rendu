local sep = "/"
local ext = ""
local copyFix = ""
if os.ishost("windows") then
	sep = "\\"
	ext = ".exe"
	copyFix = "*"
end
cwd = os.getcwd()

-- Workspace definition.

workspace("GL_Template")
	-- Configurations
	configurations({ "Debug", "Release"})
	location("build")
	targetdir ("build/%{prj.name}/%{cfg.longname}")
	debugdir ("build/%{prj.name}/%{cfg.longname}")
	architecture("x64")

	-- Configuration specific settings.
	filter("configurations:Debug")
		defines({ "DEBUG" })
		symbols("On")
	filter("configurations:Release")
		defines({ "NDEBUG" })
		optimize("On")
	filter({})


-- Helper functions for the projects.

function InstallProject(projectName, destination)
	filter("configurations:Debug")
		postbuildcommands({
			path.translate( "{CHDIR} "..os.getcwd(), sep),
			path.translate( "{COPY} build/"..projectName.."/Debug/"..projectName..ext.." "..destination..copyFix, sep)
		})
	filter("configurations:Release")
		postbuildcommands({
			path.translate( "{CHDIR} "..os.getcwd(), sep),
			path.translate( "{COPY} build/"..projectName.."/Release/"..projectName..ext.." "..destination..copyFix, sep)
		})
	filter({})
end

function CPPSetup()
	language("C++")
	cppdialect("C++11")
	buildoptions({ "-Wall" })
end	

function GraphicsSetup()
	CPPSetup()

	-- To support angled brackets in Xcode.
	sysincludedirs({ "src/libs/", "src/libs/glfw/include/" })

	-- Libraries for each platform.
	if os.istarget("macosx") then
		libdirs({"src/libs/glfw/lib-mac/", "src/libs/nfd/lib-mac/"})
		links({"glfw3", "nfd", "OpenGL.framework", "Cocoa.framework", "IOKit.framework", "CoreVideo.framework", "AppKit.framework"})
	elseif os.istarget("windows") then
		libdirs({"src/libs/glfw/lib-win-x64/", "src/libs/nfd/lib-win-x64/"})
		links({"glfw3", "nfd", "opengl32", "comctl32"})
	else -- Assume linux
		-- Libraries needed: OpenGL and glfw3.  glfw3 require X11, Xi, and so on...	
		libdirs({ os.findlib("glfw3"), os.findlib("nfd") })
		links({"glfw3", "nfd", "GL", "X11", "Xi", "Xrandr", "Xxf86vm", "Xinerama", "Xcursor", "rt", "m", "pthread", "dl", "gtk+-3.0"})
	end

end

function ShaderValidation()
	-- Run the shader validator on all existing shaders.
	-- Output IDE compatible error messages.
	dependson({"ShaderValidator"})
	prebuildcommands({ 
		-- Move to the build directory.
		path.translate("{CHDIR} "..os.getcwd().."/build", sep),
		-- Run the shader validator on the resources directory.
		path.translate( "./shader_validator"..ext.." "..cwd.."/resources/", sep)
	})
end	

function RegisterSourcesAndResources(srcPath, rscPath)
	files({ srcPath, rscPath })
	removefiles({"**.DS_STORE", "**.thumbs"})
	-- Reorganize file hierarchy in the IDE project.
	vpaths({
	   ["*"] = {srcPath},
	   ["Resources/*"] = {rscPath}
	})
end

function AppSetup(appName)
	GraphicsSetup()
	includedirs({ "src/engine" })
	links({"Engine"})
	kind("ConsoleApp")
	ShaderValidation()
	-- Declare src and resources files.
	srcPath = "src/apps/"..appName.."/**"
	rscPath = "resources/"..appName.."/**"
	RegisterSourcesAndResources(srcPath, rscPath)
end	

function ToolSetup(toolName)
	GraphicsSetup()
	includedirs({ "src/engine" })
	links({"Engine"})
	kind("ConsoleApp")
	--location("build/tools")
end	

-- Projects

project("Engine")
	GraphicsSetup()
	includedirs({ "src/engine" })
	kind("StaticLib")
	files({ "src/engine/**.hpp", "src/engine/**.cpp",
			"resources/common/**", "resources/common/**", "resources/common/**",
			"src/libs/*/*.hpp", "src/libs/*/*.cpp", "src/libs/*/*.h",
			"premake5.lua"
	})
	removefiles({"**.DS_STORE", "**.thumbs"})
	-- Virtual path allow us to get rid of the on-disk hierarchy.
	vpaths({
	   ["engine/*"] = {"src/engine/**"},
	   ["Resources/*"] = {"resources/common/**"},
	   ["Libraries/*"] = {"src/libs/**"},
	})


group("Apps")

project("PBRDemo")
	AppSetup("pbrdemo")
	
project("Playground")
	AppSetup("playground")

project("Atmosphere")
	AppSetup("atmosphere")
	
project("ImageViewer")
	AppSetup("imageviewer")

project("SnakeGame")
	AppSetup("snakegame")

	
	
group("Tools")

project("AtmosphericScatteringEstimator")
	ToolSetup()
	files({ "src/tools/AtmosphericScatteringEstimator.cpp" })

project("BRDFEstimator")
	ToolSetup()
	includedirs({ "src/apps/pbrdemo" })
	files({ "src/tools/BRDFEstimator.cpp" })

project("ControllerTest")
	ToolSetup()
	files({ "src/tools/controllertest/**.hpp", "src/tools/controllertest/**.cpp", })

project("ShaderValidator")
	ToolSetup()	
	files({ "src/tools/ShaderValidator.cpp" })
	-- Install the shader validation utility in the root build directory.
	InstallProject("%{prj.name}", "build/shader_validator"..ext)
	filter({})


group("Meta")

project("ALL")
	CPPSetup()
	kind("ConsoleApp")
	dependson( {"Engine", "PBRDemo", "Playground", "Atmosphere", "ImageViewer", "AtmosphericScatteringEstimator", "BRDFEstimator", "ControllerTest", "SnakeGame", })

-- Actions

newaction {
   trigger     = "clean",
   description = "Clean the build directory",
   execute     = function ()
      print("Cleaning...")
      os.rmdir("./build")
      print("Done.")
   end
}

newaction {
   trigger     = "docs",
   description = "Build the documentation using Doxygen",
   execute     = function ()
      print("Generating documentation...")
      os.execute("doxygen"..ext.." docs/Doxyfile")
      print("Done.")
   end
}

-- TODO: package resources automatically.
	
