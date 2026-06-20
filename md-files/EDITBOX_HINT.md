## Nuklear edit boxes

Always use `frontend_edit_replace()` (frontend.c) instead of calling
`nk_edit_string_zero_terminated()` directly. It strips `NK_EDIT_ALWAYS_INSERT_MODE`
(so the field starts in overwrite/replace mode), sets `NK_TEXT_EDIT_MODE_REPLACE`
each active frame, and calls `nk_edit_unfocus()` on `NK_EDIT_COMMITED` so the
cursor releases on Enter. Direct calls only appear inside `frontend_edit_replace`
itself and `frontend_draw_register_edit` (which has its own inline version).