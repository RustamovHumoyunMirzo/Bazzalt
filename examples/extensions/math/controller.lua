local Math = Bazzalt.require("ExampleMath")
local Tool = {}
function Tool:Open() Editor:OpenPanel("panel") end
function Tool:OnClicked(name)
    if name == "runButton" then
        local value = tonumber(Editor:GetText("valueInput"))
        Editor:SetText("resultLabel", value and tostring(Math.Double(value)) or "Enter a number")
    end
end
return Tool
