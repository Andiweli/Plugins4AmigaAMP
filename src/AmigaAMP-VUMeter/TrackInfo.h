struct TrackInfo {
	UBYTE	private[4];  /* DO NOT USE! */
	ULONG	StreamSize;
	ULONG	HeaderSize;
	ULONG	Length;
	ULONG	Frequency;
	UBYTE	Layer;
	UBYTE	Version;
	UBYTE	CRCs;
	UBYTE	Emphasis;
	UBYTE	Private;
	UBYTE	Copyright;
	UBYTE	Original;
	UBYTE	Mode;
	UWORD	Bitrate;
	UWORD	Channels;

	char	*TrackInfoText;  /* same text as displayed in AmigaAMPs TrackInfo line */
	char	*ID3title;
	char	*ID3artist;
	char	*ID3album;
	char	*ID3year;
	char	*ID3comment;
	char	*ID3genre;
	ULONG	Position;
	UWORD	TrackNumber;
	UWORD	DriveMode;       /* 0=STOP, 1=PLAY, 3=PAUSE */
	UWORD	Hardware;        /* 0=none, 1=MPEGA, 2=PowerUP, 3=External, 4=MPEGit */
};
