// TNewCheckListBox reserves a legacy bitmap-sized check slot, but the native
// theme may draw a wider glyph at high DPI. Offset must leave room inside the
// list client area; moving the whole list does not fix that clipping.
procedure InitializeWizard();
begin
  WizardForm.TasksList.Offset := ScaleX(12);

  // Use the existing description/body inset for the heading too. Preserve
  // its right edge so that it stays clear of the wizard artwork at any DPI.
  WizardForm.PageNameLabel.Width := WizardForm.PageNameLabel.Width -
    (WizardForm.PageDescriptionLabel.Left - WizardForm.PageNameLabel.Left);
  WizardForm.PageNameLabel.Left := WizardForm.PageDescriptionLabel.Left;

  // Bring the body slightly closer to the header without changing the
  // window size, text, or native page behavior. Keep its bottom edge fixed.
  WizardForm.InnerNotebook.Top := WizardForm.InnerNotebook.Top - ScaleY(8);
  WizardForm.InnerNotebook.Height := WizardForm.InnerNotebook.Height + ScaleY(8);

  // Tighten only the gap before the shortcut group; retain the high-DPI
  // checkbox inset above and the native spacing within the group.
  WizardForm.TasksList.Top := WizardForm.TasksList.Top - ScaleY(4);
  WizardForm.TasksList.Height := WizardForm.TasksList.Height + ScaleY(4);
end;
