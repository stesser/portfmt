# You can force skipping these test by defining IGNORE_PATH_CHECKS
.if !defined(IGNORE_PATH_CHECKS)
.if ! ${PREFIX:M/*}
.BEGIN:
	@${ECHO_MSG} "PREFIX must be defined as an absolute path so that when 'make'"
	@${ECHO_MSG} "is invoked in the work area PREFIX points to the right place."
	@${FALSE}
.endif
.endif

DATADIR?=		${PREFIX}/share/${PORTNAME}
DOCSDIR?=		${PREFIX}/share/doc/${PORTNAME}
ETCDIR?=		${PREFIX}/etc/${PORTNAME}
EXAMPLESDIR?=	${PREFIX}/share/examples/${PORTNAME}
WWWDIR?=		${PREFIX}/www/${PORTNAME}
<<<<<<<<<
{ COMMENT, line 1, .comment = # You can force skipping these test by defining IGNORE_PATH_CHECKS }
{ IF/IF, line 2, .indent = 0, .test = { !, defined(, IGNORE_PATH_CHECKS, ) }, .elseif = 0 }
=> if:
	{ IF/IF, line 3, .indent = 0, .test = { !, ${PREFIX:M/*} }, .elseif = 0 }
	=> if:
		{ TARGET/NAMED, line 4, .sources = { .BEGIN }, .dependencies = {  } }
			{ TARGET_COMMAND, line 5, .words = [2]{ ${ECHO_MSG}, "PREFIX must be defined as an absolute path so that when 'make'" }, .flags = @ }
			{ TARGET_COMMAND, line 6, .words = [2]{ ${ECHO_MSG}, "is invoked in the work area PREFIX points to the right place." }, .flags = @ }
			{ TARGET_COMMAND, line 7, .words = [1]{ ${FALSE} }, .flags = @ }
{ COMMENT, line 10, .comment =  }
{ VARIABLE, line 11, .name = DATADIR, .modifier = ?=, .words = [1]{ ${PREFIX}/share/${PORTNAME} } }
{ VARIABLE, line 12, .name = DOCSDIR, .modifier = ?=, .words = [1]{ ${PREFIX}/share/doc/${PORTNAME} } }
{ VARIABLE, line 13, .name = ETCDIR, .modifier = ?=, .words = [1]{ ${PREFIX}/etc/${PORTNAME} } }
{ VARIABLE, line 14, .name = EXAMPLESDIR, .modifier = ?=, .words = [1]{ ${PREFIX}/share/examples/${PORTNAME} } }
{ VARIABLE, line 15, .name = WWWDIR, .modifier = ?=, .words = [1]{ ${PREFIX}/www/${PORTNAME} } }
