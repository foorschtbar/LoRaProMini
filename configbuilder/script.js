// Run at document ready
$(document).ready((e) => {
    makeConfig({
        event: e
    });
    checkVPBInputs();
});

// Listen for events
$("form").on('click', 'button:not(:disabled)', (e) => {
    e.preventDefault();
    let field = $(e.currentTarget).prevAll("input, textarea");
    let fieldname = $(e.currentTarget).prevAll("input, textarea").attr("id");

    if ($(e.currentTarget).hasClass("generate")) {
        genStr = "";
        if (formFields[fieldname] !== "undefined") {
            datatypeLen = formFields[fieldname][1];
            for (i = 0; i < datatypeLen; i++) {
                genStr += padHexStr(Math.floor(Math.random() * 256).toString(16), 2).toUpperCase();
            }
            field.val(genStr).change();

        }
    } else if ($(e.currentTarget).hasClass("copy")) {
        field.focus();
        field.select();
        try {
            document.execCommand('copy');
        } catch (err) {}
    } else if ($(e.currentTarget).hasClass("calc")) {
        $("#calc").toggle();
        calcVPB();

    } else if ($(e.currentTarget).hasClass("add")) {
        let masterrow = $("#calc > div:first");
        masterrow.clone().appendTo("#calc").find("input[type='text']").val("");
        calcVPB();
        checkVPBInputs();

    } else if ($(e.currentTarget).hasClass("remove")) {
        $(e.currentTarget).parent().remove();
        calcVPB();
        checkVPBInputs();
    }

});
$("form").on('keyup change', '#calc input', (e) => calcVPB({
    event: e
}));
$(":not(#calc) input, select").on('keyup change', (e) => makeConfig({
    event: e
}));
$("#configStr").on('keyup blur change', (e) => makeConfig({
    event: e,
    fromInput: true
}));


const checkVPBInputs = () => {
    $("#calc>div>button.add").prop("disabled", false).removeClass("btn-outline-secondary").addClass("btn-outline-success");
    $("#calc>div>button.remove").prop("disabled", false).removeClass("btn-outline-secondary").addClass("btn-outline-danger");
    $("#calc>div:not(:last-child)>button.add").prop("disabled", true).removeClass("btn-outline-success").addClass("btn-outline-secondary");
    $("#calc>div:only-child>button.remove").prop("disabled", true).removeClass("btn-outline-danger").addClass("btn-outline-secondary");
}

const calcVPB = (event) => {

    let fields = $("#calc input");
    let vpb = 0;
    let count = 0;
    for (i = 0; i < fields.length; i += 2) {
        let error = false;

        let f1 = $(fields[i]);
        let f2 = $(fields[i + 1]);
        let voltage = parseFloat(f1.val());
        let analog = parseFloat(f2.val());

        if (voltage == "" || isNaN(voltage) || f1.val().match(/,/g)) {
            error = true;
            f1.removeClass("is-valid").addClass("is-invalid");
        } else {
            f1.removeClass("is-invalid").addClass("is-valid");
        }

        if (analog == "" || isNaN(analog) || f2.val().match(/,/g)) {
            error = true;
            f2.removeClass("is-valid").addClass("is-invalid");
        } else {
            f2.removeClass("is-invalid").addClass("is-valid");
        }

        if (!error) {
            vpb += (voltage / analog);
            count++;
        }
    }

    if (count > 0) {
        vpb /= count;
    }

    $("#BAT_SENSE_VPB").val(vpb).change();
}

const getConfigLen = () => {
    let len = 0;
    Object.keys(formFields).forEach((k) => {
        len += parseInt(formFields[k][1]) * 2;
    });
    return len;
}

const getValidLen = (str) => {
    patt = /[^\s-]/g
    validLen = str.length;
    while (match = patt.exec(str)) {
        validLen = patt.lastIndex;
    }
    return validLen;
}

const makeConfig = ({
    event,
    fromInput = false
}) => {
    let errorMake = false;
    let errorMsg = "";
    let cfgStr = "";
    let pos = 0;
    let configStr = $("#configStr");
    let actMethode = parseInt($("#ACTIVATION_METHOD").val());
    let configLen = getConfigLen();
    let configLenWithCRC = getConfigLen() + 8;

    if (fromInput) {
        // Clean input
        configStr.val(configStr.val().replace(/[^a-fA-F0-9]/g, ""));

        if (configStr.val().length == 0) {
            return;
        } else if (configStr.val().length != configLen && configStr.val().length != configLenWithCRC) {
            errorMsg = "Invalid lenght (currently " + configStr.val().length + " chars, expected are " + configLen + " or " + configLenWithCRC + " with checksum)";
        }
    }

    if (errorMsg == "") {
        Object.keys(formFields).forEach((k) => {
                let hexStr = "";
                errorMsg = "";

                let field = $("#" + k);
                let fieldFeedback = field.nextAll("div.invalid-feedback");
                let fieldBtn = field.nextAll("button");

                let datatype = formFields[k][0];
                let datatypeSize = parseInt(formFields[k][1]);
                let dataFlipped = formFields[k][2];
                let dataGroup = formFields[k][3];

                if (field.length) {
                    if (fromInput) {

                        // Extract bytes from configStr
                        hexSubStr = configStr.val().substr(pos, datatypeSize * 2);

                        // Flip value
                        if (dataFlipped) {
                            hexSubStr = flipHexString(hexSubStr, hexSubStr.length);
                        }

                        switch (datatype) {
                            case "int":
                                field.val(parseInt(hexSubStr, 16));
                                break;
                            case "float":
                                field.val(HexToFloat32(hexSubStr));
                                break;
                            case "str":
                                field.val(hexSubStr);
                                break;
                        }

                    } else {

                        if (dataGroup > 0 && dataGroup != actMethode) {
                            field.prop("disabled", true);
                            fieldBtn.prop("disabled", true);
                            field.parent().hide();
                            field.removeClass("is-valid").removeClass("is-invalid");
                            cfgStr += padHexStr("", datatypeSize * 2);
                        } else {
                            field.parent().show();
                            field.prop("disabled", false);
                            fieldBtn.prop("disabled", false);

                            if (field.val() !== null && field.val().trim().length != 0) {

                                switch (datatype) {
                                    case "int":
                                        num = parseInt(field.val());

                                        if (!isNaN(num) && !field.val().match(/,/g)) {
                                            if (num >= 0) {
                                                hexStr = padHexStr(num.toString(16), datatypeSize * 2);

                                                if (hexStr.length > datatypeSize * 2) {
                                                    errorMsg = "Number to large";
                                                }
                                            } else {
                                                errorMsg = "Number too small";
                                            }
                                        } else {
                                            errorMsg = "Invalid number";
                                        }

                                        break;
                                    case "float":
                                        num = parseFloat(field.val());

                                        if (!isNaN(num) && !field.val().match(/,/g)) {
                                            if (num >= 0) {
                                                hexStr = Float32ToHex(num);
                                            } else {
                                                errorMsg = "Number too small";
                                            }
                                        } else {
                                            errorMsg = "Invalid number";
                                        }

                                        break;

                                    case "str":

                                        raw = field.val().replace(/[^a-fA-F0-9]/g, "")

                                        let caret = field.caret();

                                        field.val(formatHexStr(raw, datatypeSize * 2));

                                        if (field.is(":focus")) {

                                            returnkeyused = false;
                                            if (event !== null) {
                                                switch (event.which) {
                                                    case 8:
                                                    case 46:
                                                        returnkeyused = true;
                                                        break;
                                                }
                                            }

                                            validPos = getValidLen(field.val());

                                            if (!returnkeyused && ((caret + 1) % 3) == 0) {
                                                caret++;
                                            } else if (returnkeyused && (caret % 3) == 0) {
                                                caret--;
                                            }

                                            if (caret > (validPos + 1)) {
                                                caret = validPos;
                                            }
                                            console.log(field);
                                            field.caret(caret);
                                        }


                                        let re = /([a-fA-F0-9]{2})/gm;
                                        result = field.val().match(re);

                                        if (result !== null) {

                                            if (result.length == datatypeSize) {

                                                hexStr = result.join("");

                                                hexStr = padHexStr(hexStr, datatypeSize * 2);

                                            } else {
                                                errorMsg = "Value must be exactly " + datatypeSize * 2 + " characters long ";
                                            }
                                        } else {
                                            errorMsg = "Invalid input";
                                        }

                                        break;
                                }

                                hexStr = hexStr.toUpperCase();

                                if (dataFlipped) {
                                    hexStr = flipHexString(hexStr, hexStr.length);
                                }

                                if (errorMsg != "") {
                                    field.removeClass("is-valid").addClass("is-invalid");
                                    fieldFeedback.html(errorMsg);
                                    console.log("Field " + k + ": " + errorMsg);
                                    errorMake = true;
                                } else {
                                    field.removeClass("is-invalid").addClass("is-valid");
                                    cfgStr += hexStr;
                                }

                            } else {
                                field.removeClass("is-valid");
                                errorMake = true;
                            }

                            //console.log(k + " (" + datatype + ", " + datatypeSize + ", " + dataFlipped + ") " + num + " --> " + hexStr + " --> (flipped: " + flipHexString(hexStr, hexStr.length) + ")");

                        }
                    }

                } else {
                    errorMake = true;
                    console.log("Field " + k + " not found!");
                }

                pos += datatypeSize * 2;

            }

        );
    }

    if (fromInput) {

        if (errorMsg != "") {
            configStr.removeClass("is-valid").addClass("is-invalid");
            configStr.nextAll("div.invalid-feedback").html(errorMsg);
        } else {
            configStr.removeClass("is-invalid").addClass("is-valid");
            makeConfig({
                event: event
            });
        }

    } else {
        size = 0;
        sizecrc32 = 0;
        cfgStrCRC32 = "";

        if (cfgStr != "" && !errorMake) {
            cfgStrCRC32 = padHexStr(crc32(cfgStr).toString(16), 8).toUpperCase();

            size = Math.floor(cfgStr.length / 2);
            sizecrc32 = size + Math.floor(cfgStrCRC32.length / 2);

            cfgStr = cfgStr + cfgStrCRC32;

            configStr.removeClass("is-invalid").addClass("is-valid");

        } else {
            cfgStr = "";
            cfgStrCRC32 = "---";
            // if ($(".is-invalid").length) {
            configStr.removeClass("is-valid").removeClass("is-invalid"); //.addClass("is-invalid");
            // }

        }



        configStr.val(cfgStr);
        $("#configCRC32").val(cfgStrCRC32);
        $("#configSize").val(size);
        $("#configSizeCRC").val(sizecrc32);
    }


}

// https://gist.github.com/Jozo132/2c0fae763f5dc6635a6714bb741d152f
const Float32ToHex = (float32) => {
    const getHex = i => padHexStr(i.toString(16), 2);
    var view = new DataView(new ArrayBuffer(4));
    view.setFloat32(0, float32);

    return Array.apply(null, {
            length: 4
        }

    ).map((_, i) => getHex(view.getUint8(i))).join('');
}

const HexToFloat32 = (str) => {
    var int = parseInt(str, 16);

    if (int > 0 || int < 0) {
        var sign = (int >>> 31) ? -1 : 1;
        var exp = (int >>> 23 & 0xff) - 127;
        var mantissa = ((int & 0x7fffff) + 0x800000).toString(2);

        var float32 = 0
        for (i = 0; i < mantissa.length; i += 1) {
            float32 += parseInt(mantissa[i]) ? Math.pow(2, exp) : 0;
            exp--
        }

        return float32 * sign;
    } else return 0
}

const flipHexString = (hexStr, hexDigits) => {
    h = "";

    for (let i = 0; i < hexDigits; ++i) {
        h += hexStr.substr((hexDigits - 1 - i) * 2, 2);
    }

    return h;
}

const formatHexStr = (hexStr, length) => {
    let strRtn = "";
    for (i = 0; i < length; i++) {
        if (i < hexStr.length) {
            strRtn += hexStr.substr(i, 1);
        } else {
            strRtn += "-"
        }
        if (i < length - 1 && i % 2) {
            strRtn += " ";
        }
    }
    return strRtn;
}

const padHexStr = (hexStr, length) => {

    while (hexStr.length < length) {
        hexStr = "0" + hexStr;
    }

    return hexStr;
}

const makeCRCTable = () => {
    var c;
    var crcTable = [];

    for (var n = 0; n < 256; n++) {
        c = n;

        for (var k = 0; k < 8; k++) {
            c = ((c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1));
        }

        crcTable[n] = c;
    }

    return crcTable;
}

const crc32 = (str) => {
    var crcTable = window.crcTable || (window.crcTable = makeCRCTable());
    var crc = 0 ^ (-1);

    for (var i = 0; i < str.length; i++) {
        crc = (crc >>> 8) ^ crcTable[(crc ^ str.charCodeAt(i)) & 0xFF];
    }

    return (crc ^ (-1)) >>> 0;
}