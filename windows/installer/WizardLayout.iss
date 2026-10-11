// TNewCheckListBox reserves a legacy bitmap-sized check slot, but the native
// theme may draw a wider glyph at high DPI. Offset must leave room inside the
// list client area; moving the whole list does not fix that clipping.
procedure InitializeWizard();
begin
  WizardForm.TasksList.Offset := ScaleX(12);
end;
