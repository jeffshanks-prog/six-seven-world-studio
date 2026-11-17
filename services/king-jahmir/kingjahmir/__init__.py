"""King Jahmir: the 6|7 World Studio AI assistant's backend gateway.

The viewer never holds an AI key. It sends the user's message here with the
user's 6|7 session token; this gateway checks the token, applies rate limits,
retrieves help pages, runs permission-checked tools and calls Claude.
"""

__version__ = "0.1.0"
