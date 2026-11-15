section .rodata
global blob_rtc, blob_rtc_end
global blob_beep, blob_beep_end
global blob_crc, blob_crc_end
global blob_bug, blob_bug_end

blob_rtc:      incbin "rtc.bin"
blob_rtc_end:
blob_beep:     incbin "beep.bin"
blob_beep_end:
blob_crc:      incbin "crc.bin"
blob_crc_end:
blob_bug:      incbin "bug.bin"
blob_bug_end:
