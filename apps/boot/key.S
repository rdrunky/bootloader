	.section .rodata.trusted_key, "a"
	.global trusted_key
	.hidden trusted_key
	.type trusted_key, %object
trusted_key:
	.incbin PUBLIC_KEY
	.size trusted_key, . - trusted_key
	.if . - trusted_key != 32
	.error "the trusted key must be 32 bytes"
	.endif
