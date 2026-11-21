section .rodata
global blob_mbr, blob_mbr_end
global blob_stage2, blob_stage2_end

blob_mbr:       incbin "mbr.bin"
blob_mbr_end:
blob_stage2:    incbin "stage2.bin"
blob_stage2_end:
