local function dispatcher()
    try
        print("In try")
        error("CONTINUE")
    catch(e)
        print("Caught", e)
    end
end
dispatcher()
