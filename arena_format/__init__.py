"""Experimental, bounded layered-arena validation and read-only feasibility queries.

This module does not load CAD, modify simulations, execute rules, or plan motion.
All distances are millimetres. See README.md for the deliberate model limits.
"""
from .core import (ArenaValidationError, MAX_TEXT_BYTES, SCHEMA, parse_document,
                   query, validate_document)

__all__ = ['ArenaValidationError', 'MAX_TEXT_BYTES', 'SCHEMA', 'parse_document',
           'query', 'validate_document']
