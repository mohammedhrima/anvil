
+ mention installing readline


src = "/folder1"
dist = "/folder2"

src = "/folder1"
dist = "/folder2"

if anvil.os == "linux" {
    build:Cmd = "clang main.c"
    copy:Cmd = fn(arg: String) {
        file: File = open_file(arg).on({
            success = {

            },
            failure = {
                print("failed to open ", arg, "\n")
                exit(1)
            } 
        })

        content: String = file.readline()
        trim: String = content.trim()
    }
}
else {
    print("unknown os\n")
}
