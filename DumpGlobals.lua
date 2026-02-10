local visited = {}
local files = {
    base = {},
    classfunc = {},
    globalf = {},
    globalns = {},
    globalv = {}
}

-- Predefined base/keywords
local keywords = {"if", "then", "else", "elseif", "end", "for", "while", "do", "repeat", "until", "function", "local", "return", "break", "true", "false", "nil", "and", "or", "not"}
for _, k in ipairs(keywords) do table.insert(files.base, k) end

local function scan(tbl, name, isGlobal)
    if type(tbl) ~= "table" and type(tbl) ~= "userdata" then return end
    if visited[tbl] or (name and name:find("%.%.") ) then return end
    visited[tbl] = true
    
    -- pcall handles MoonSharp userdata that might not support iteration
    pcall(function()
        for k, v in pairs(tbl) do
            local keyName = tostring(k)
            -- Skip internal members and meta-properties
            if type(k) == "string" and not keyName:find("^__") then
                local fullName = isGlobal and keyName or (name .. "." .. keyName)
                local t = type(v)
                
                if isGlobal then
                    if t == "function" then
                        table.insert(files.globalf, fullName)
                    elseif t == "userdata" then
                        table.insert(files.globalns, fullName)
                        -- Recursively scan userdata in MoonSharp as they often act like tables (CLR objects)
                        scan(v, fullName, false)
                    elseif t == "table" then
                        table.insert(files.globalv, fullName)
                        -- Avoid infinite recursion in common global tables
                        if not (fullName == "_G" or fullName == "shared" or fullName == "_VERSION" or fullName == "package") then
                            scan(v, fullName, false)
                        end
                    else
                        table.insert(files.globalv, fullName)
                    end
                else
                    -- Nested objects (properties/methods)
                    if t == "function" then
                        table.insert(files.classfunc, fullName)
                    elseif t == "table" or t == "userdata" then
                        table.insert(files.globalv, fullName)
                        scan(v, fullName, false)
                    end
                end

                -- Detect methods in metatables (useful for MoonSharp interop objects)
                pcall(function()
                    local mt = getmetatable(v)
                    if mt and mt.__index then
                        local index = mt.__index
                        if type(index) == "table" then
                            for mk, mv in pairs(index) do
                                if type(mv) == "function" and type(mk) == "string" then
                                    table.insert(files.classfunc, fullName .. ":" .. mk)
                                end
                            end
                        end
                    end
                end)
            end
        end
    end)
end

-- Start scanning from _G
scan(_G, "", true)

-- Save function with safety fallbacks
local function save(filename, list)
    if #list == 0 then return end
    local content = table.concat(list, "\n")
    
    if writefile then
        pcall(writefile, filename, content)
    elseif io and io.open then
        pcall(function()
            local f = io.open(filename, "w")
            if f then
                f:write(content)
                f:close()
            end
        end)
    end
end

-- Export results
save("base.txt", files.base)
save("classfunc.txt", files.classfunc)
save("globalf.txt", files.globalf)
save("globalns.txt", files.globalns)
save("globalv.txt", files.globalv)

if print then print("Dump complete! Files generated in workspace.") end
